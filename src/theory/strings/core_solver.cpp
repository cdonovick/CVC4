/*********************                                                        */
/*! \file theory_strings.cpp
 ** \verbatim
 ** Top contributors (to current version):
 **   Andrew Reynolds, Tianyi Liang, Morgan Deters
 ** This file is part of the CVC4 project.
 ** Copyright (c) 2009-2019 by the authors listed in the file AUTHORS
 ** in the top-level source directory) and their institutional affiliations.
 ** All rights reserved.  See the file COPYING in the top-level source
 ** directory for licensing information.\endverbatim
 **
 ** \brief Implementation of the theory of strings.
 **
 ** Implementation of the theory of strings.
 **/

#include "theory/strings/core_solver.h"

#include "options/strings_options.h"
#include "theory/strings/sequences_rewriter.h"
#include "theory/strings/strings_entail.h"
#include "theory/strings/theory_strings_utils.h"
#include "theory/strings/word.h"

using namespace std;
using namespace CVC4::context;
using namespace CVC4::kind;

namespace CVC4 {
namespace theory {
namespace strings {

CoreInferInfo::CoreInferInfo() : d_index(0), d_rev(false) {}

CoreSolver::CoreSolver(context::Context* c,
                       context::UserContext* u,
                       SolverState& s,
                       InferenceManager& im,
                       TermRegistry& tr,
                       BaseSolver& bs)
    : d_state(s), d_im(im), d_termReg(tr), d_bsolver(bs), d_nfPairs(c)
{
  d_zero = NodeManager::currentNM()->mkConst( Rational( 0 ) );
  d_one = NodeManager::currentNM()->mkConst( Rational( 1 ) );
  d_neg_one = NodeManager::currentNM()->mkConst(Rational(-1));
  d_true = NodeManager::currentNM()->mkConst( true );
  d_false = NodeManager::currentNM()->mkConst( false );
}

CoreSolver::~CoreSolver() {

}

void CoreSolver::debugPrintFlatForms( const char * tc ){
  for( unsigned k=0; k<d_strings_eqc.size(); k++ ){
    Node eqc = d_strings_eqc[k];
    if( d_eqc[eqc].size()>1 ){
      Trace( tc ) << "EQC [" << eqc << "]" << std::endl;
    }else{
      Trace( tc ) << "eqc [" << eqc << "]";
    }
    Node c = d_bsolver.getConstantEqc(eqc);
    if( !c.isNull() ){
      Trace( tc ) << "  C: " << c;
      if( d_eqc[eqc].size()>1 ){
        Trace( tc ) << std::endl;
      }
    }
    if( d_eqc[eqc].size()>1 ){
      for( unsigned i=0; i<d_eqc[eqc].size(); i++ ){
        Node n = d_eqc[eqc][i];
        Trace( tc ) << "    ";
        for( unsigned j=0; j<d_flat_form[n].size(); j++ ){
          Node fc = d_flat_form[n][j];
          Node fcc = d_bsolver.getConstantEqc(fc);
          Trace( tc ) << " ";
          if( !fcc.isNull() ){
            Trace( tc ) << fcc;
          }else{
            Trace( tc ) << fc;
          }
        }
        if( n!=eqc ){
          Trace( tc ) << ", from " << n;
        }
        Trace( tc ) << std::endl;
      }
    }else{
      Trace( tc ) << std::endl;
    }
  }
  Trace( tc ) << std::endl;
}

struct sortConstLength {
  std::map< Node, unsigned > d_const_length;
  bool operator() (Node i, Node j) {
    std::map< Node, unsigned >::iterator it_i = d_const_length.find( i );
    std::map< Node, unsigned >::iterator it_j = d_const_length.find( j );
    if( it_i==d_const_length.end() ){
      if( it_j==d_const_length.end() ){
        return i<j;
      }else{
        return false;
      }
    }else{
      if( it_j==d_const_length.end() ){
        return true;
      }else{
        return it_i->second<it_j->second;
      }
    }
  }
};

void CoreSolver::checkCycles()
{
  // first check for cycles, while building ordering of equivalence classes
  d_flat_form.clear();
  d_flat_form_index.clear();
  d_eqc.clear();
  // Rebuild strings eqc based on acyclic ordering, first copy the equivalence
  // classes from the base solver.
  const std::vector<Node>& eqc = d_bsolver.getStringEqc();
  d_strings_eqc.clear();
  for (const Node& r : eqc)
  {
    std::vector< Node > curr;
    std::vector< Node > exp;
    checkCycles(r, curr, exp);
    if (d_im.hasProcessed())
    {
      return;
    }
  }
}

void CoreSolver::checkFlatForms()
{
  // debug print flat forms
  if (Trace.isOn("strings-ff"))
  {
    Trace("strings-ff") << "Flat forms : " << std::endl;
    debugPrintFlatForms("strings-ff");
  }

  // inferences without recursively expanding flat forms

  //(1) approximate equality by containment, infer conflicts
  for (const Node& eqc : d_strings_eqc)
  {
    Node c = d_bsolver.getConstantEqc(eqc);
    if (!c.isNull())
    {
      // if equivalence class is constant, all component constants in flat forms
      // must be contained in it, in order
      std::map<Node, std::vector<Node> >::iterator it = d_eqc.find(eqc);
      if (it != d_eqc.end())
      {
        for (const Node& n : it->second)
        {
          int firstc, lastc;
          if (!StringsEntail::canConstantContainList(
                  c, d_flat_form[n], firstc, lastc))
          {
            Trace("strings-ff-debug") << "Flat form for " << n
                                      << " cannot be contained in constant "
                                      << c << std::endl;
            Trace("strings-ff-debug") << "  indices = " << firstc << "/"
                                      << lastc << std::endl;
            // conflict, explanation is n = base ^ base = c ^ relevant portion
            // of ( n = f[n] )
            std::vector<Node> exp;
            d_bsolver.explainConstantEqc(n,eqc,exp);
            for (int e = firstc; e <= lastc; e++)
            {
              if (d_flat_form[n][e].isConst())
              {
                Assert(e >= 0 && e < (int)d_flat_form_index[n].size());
                Assert(d_flat_form_index[n][e] >= 0
                       && d_flat_form_index[n][e] < (int)n.getNumChildren());
                d_im.addToExplanation(
                    d_flat_form[n][e], n[d_flat_form_index[n][e]], exp);
              }
            }
            Node conc = d_false;
            d_im.sendInference(exp, conc, Inference::F_NCTN);
            return;
          }
        }
      }
    }
  }

  //(2) scan lists, unification to infer conflicts and equalities
  for (const Node& eqc : d_strings_eqc)
  {
    std::map<Node, std::vector<Node> >::iterator it = d_eqc.find(eqc);
    if (it == d_eqc.end() || it->second.size() <= 1)
    {
      continue;
    }
    // iterate over start index
    for (unsigned start = 0; start < it->second.size() - 1; start++)
    {
      for (unsigned r = 0; r < 2; r++)
      {
        bool isRev = r == 1;
        checkFlatForm(it->second, start, isRev);
        if (d_state.isInConflict())
        {
          return;
        }

        for (const Node& n : it->second)
        {
          std::reverse(d_flat_form[n].begin(), d_flat_form[n].end());
          std::reverse(d_flat_form_index[n].begin(),
                       d_flat_form_index[n].end());
        }
      }
    }
  }
}

void CoreSolver::checkFlatForm(std::vector<Node>& eqc,
                                  size_t start,
                                  bool isRev)
{
  size_t count = 0;
  // We check for flat form inferences involving `eqc[start]` and terms past
  // `start`. If there was a flat form inference involving `eqc[start]` and a
  // term at a smaller index `i`, we would have found it with when `start` was
  // `i`. Thus, we mark the preceeding terms in the equivalence class as
  // ineligible.
  std::vector<Node> inelig(eqc.begin(), eqc.begin() + start + 1);
  Node a = eqc[start];
  Trace("strings-ff-debug")
      << "Check flat form for a = " << a << ", whose flat form is "
      << d_flat_form[a] << ")" << std::endl;
  Node b;
  do
  {
    std::vector<Node> exp;
    Node conc;
    Inference infType = Inference::NONE;
    size_t eqc_size = eqc.size();
    size_t asize = d_flat_form[a].size();
    if (count == asize)
    {
      for (size_t i = start + 1; i < eqc_size; i++)
      {
        b = eqc[i];
        if (std::find(inelig.begin(), inelig.end(), b) != inelig.end())
        {
          continue;
        }

        size_t bsize = d_flat_form[b].size();
        if (count < bsize)
        {
          Trace("strings-ff-debug")
              << "Found endpoint (in a) with non-empty b = " << b
              << ", whose flat form is " << d_flat_form[b] << std::endl;
          // endpoint
          Node emp = Word::mkEmptyWord(a.getType());
          std::vector<Node> conc_c;
          for (unsigned j = count; j < bsize; j++)
          {
            conc_c.push_back(b[d_flat_form_index[b][j]].eqNode(emp));
          }
          Assert(!conc_c.empty());
          conc = utils::mkAnd(conc_c);
          infType = Inference::F_ENDPOINT_EMP;
          Assert(count > 0);
          // swap, will enforce is empty past current
          a = eqc[i];
          b = eqc[start];
          break;
        }
        inelig.push_back(eqc[i]);
      }
    }
    else
    {
      Node curr = d_flat_form[a][count];
      Node curr_c = d_bsolver.getConstantEqc(curr);
      Node ac = a[d_flat_form_index[a][count]];
      std::vector<Node> lexp;
      Node lcurr = d_state.getLength(ac, lexp);
      for (size_t i = start + 1; i < eqc_size; i++)
      {
        b = eqc[i];
        if (std::find(inelig.begin(), inelig.end(), b) != inelig.end())
        {
          continue;
        }

        if (count == d_flat_form[b].size())
        {
          inelig.push_back(b);
          Trace("strings-ff-debug")
              << "Found endpoint in b = " << b << ", whose flat form is "
              << d_flat_form[b] << std::endl;
          // endpoint
          Node emp = Word::mkEmptyWord(a.getType());
          std::vector<Node> conc_c;
          for (size_t j = count; j < asize; j++)
          {
            conc_c.push_back(a[d_flat_form_index[a][j]].eqNode(emp));
          }
          Assert(!conc_c.empty());
          conc = utils::mkAnd(conc_c);
          infType = Inference::F_ENDPOINT_EMP;
          Assert(count > 0);
          break;
        }
        else
        {
          Node cc = d_flat_form[b][count];
          if (cc != curr)
          {
            Node bc = b[d_flat_form_index[b][count]];
            inelig.push_back(b);
            Assert(!d_state.areEqual(curr, cc));
            Node cc_c = d_bsolver.getConstantEqc(cc);
            if (!curr_c.isNull() && !cc_c.isNull())
            {
              // check for constant conflict
              size_t index;
              Node s = Word::splitConstant(cc_c, curr_c, index, isRev);
              if (s.isNull())
              {
                d_bsolver.explainConstantEqc(ac,curr,exp);
                d_bsolver.explainConstantEqc(bc,cc,exp);
                conc = d_false;
                infType = Inference::F_CONST;
                break;
              }
            }
            else if ((d_flat_form[a].size() - 1) == count
                     && (d_flat_form[b].size() - 1) == count)
            {
              conc = ac.eqNode(bc);
              infType = Inference::F_ENDPOINT_EQ;
              break;
            }
            else
            {
              // if lengths are the same, apply LengthEq
              std::vector<Node> lexp2;
              Node lcc = d_state.getLength(bc, lexp2);
              if (d_state.areEqual(lcurr, lcc))
              {
                if (Trace.isOn("strings-ff-debug"))
                {
                  Trace("strings-ff-debug")
                      << "Infer " << ac << " == " << bc << " since " << lcurr
                      << " == " << lcc << std::endl;
                  Trace("strings-ff-debug")
                      << "Explanation for " << lcurr << " is ";
                  for (size_t j = 0; j < lexp.size(); j++)
                  {
                    Trace("strings-ff-debug") << lexp[j] << std::endl;
                  }
                  Trace("strings-ff-debug")
                      << "Explanation for " << lcc << " is ";
                  for (size_t j = 0; j < lexp2.size(); j++)
                  {
                    Trace("strings-ff-debug") << lexp2[j] << std::endl;
                  }
                }

                exp.insert(exp.end(), lexp.begin(), lexp.end());
                exp.insert(exp.end(), lexp2.begin(), lexp2.end());
                d_im.addToExplanation(lcurr, lcc, exp);
                conc = ac.eqNode(bc);
                infType = Inference::F_UNIFY;
                break;
              }
            }
          }
        }
      }
    }
    if (!conc.isNull())
    {
      Trace("strings-ff-debug") << "Found inference (" << infType
                                << "): " << conc << " based on equality " << a
                                << " == " << b << ", " << isRev << std::endl;
      d_im.addToExplanation(a, b, exp);
      // explain why prefixes up to now were the same
      for (size_t j = 0; j < count; j++)
      {
        Trace("strings-ff-debug") << "Add at " << d_flat_form_index[a][j] << " "
                                  << d_flat_form_index[b][j] << std::endl;
        d_im.addToExplanation(
            a[d_flat_form_index[a][j]], b[d_flat_form_index[b][j]], exp);
      }
      // explain why other components up to now are empty
      for (unsigned t = 0; t < 2; t++)
      {
        Node c = t == 0 ? a : b;
        ssize_t jj;
        if (infType == Inference::F_ENDPOINT_EQ
            || (t == 1 && infType == Inference::F_ENDPOINT_EMP))
        {
          // explain all the empty components for F_EndpointEq, all for
          // the short end for F_EndpointEmp
          jj = isRev ? -1 : c.getNumChildren();
        }
        else
        {
          jj = t == 0 ? d_flat_form_index[a][count]
                      : d_flat_form_index[b][count];
        }
        ssize_t startj = isRev ? jj + 1 : 0;
        ssize_t endj = isRev ? c.getNumChildren() : jj;
        Node emp = Word::mkEmptyWord(a.getType());
        for (ssize_t j = startj; j < endj; j++)
        {
          if (d_state.areEqual(c[j], emp))
          {
            d_im.addToExplanation(c[j], emp, exp);
          }
        }
      }
      // Notice that F_EndpointEmp is not typically applied, since
      // strict prefix equality ( a.b = a ) where a,b non-empty
      // is conflicting by arithmetic len(a.b)=len(a)+len(b)!=len(a)
      // when len(b)!=0. Although if we do not infer this conflict eagerly,
      // it may be applied (see #3272).
      d_im.sendInference(exp, conc, infType);
      if (d_state.isInConflict())
      {
        return;
      }
      break;
    }
    count++;
  } while (inelig.size() < eqc.size());
}

Node CoreSolver::checkCycles( Node eqc, std::vector< Node >& curr, std::vector< Node >& exp ){
  if( std::find( curr.begin(), curr.end(), eqc )!=curr.end() ){
    // a loop
    return eqc;
  }else if( std::find( d_strings_eqc.begin(), d_strings_eqc.end(), eqc )==d_strings_eqc.end() ){
    curr.push_back( eqc );
    Node emp = Word::mkEmptyWord(eqc.getType());
    //look at all terms in this equivalence class
    eq::EqualityEngine* ee = d_state.getEqualityEngine();
    eq::EqClassIterator eqc_i = eq::EqClassIterator( eqc, ee );
    while( !eqc_i.isFinished() ) {
      Node n = (*eqc_i);
      if( !d_bsolver.isCongruent(n) ){
        if( n.getKind() == kind::STRING_CONCAT ){
          Trace("strings-cycle") << eqc << " check term : " << n << " in " << eqc << std::endl;
          if (eqc != emp)
          {
            d_eqc[eqc].push_back( n );
          }
          for( unsigned i=0; i<n.getNumChildren(); i++ ){
            Node nr = d_state.getRepresentative(n[i]);
            if (eqc == emp)
            {
              //for empty eqc, ensure all components are empty
              if (nr != emp)
              {
                std::vector<Node> exps;
                exps.push_back(n.eqNode(emp));
                d_im.sendInference(
                    exps, n[i].eqNode(emp), Inference::I_CYCLE_E);
                return Node::null();
              }
            }else{
              if (nr != emp)
              {
                d_flat_form[n].push_back( nr );
                d_flat_form_index[n].push_back( i );
              }
              //for non-empty eqc, recurse and see if we find a loop
              Node ncy = checkCycles( nr, curr, exp );
              if( !ncy.isNull() ){
                Trace("strings-cycle") << eqc << " cycle: " << ncy << " at " << n << "[" << i << "] : " << n[i] << std::endl;
                d_im.addToExplanation(n, eqc, exp);
                d_im.addToExplanation(nr, n[i], exp);
                if( ncy==eqc ){
                  //can infer all other components must be empty
                  for( unsigned j=0; j<n.getNumChildren(); j++ ){
                    //take first non-empty
                    if (j != i && !d_state.areEqual(n[j], emp))
                    {
                      d_im.sendInference(
                          exp, n[j].eqNode(emp), Inference::I_CYCLE);
                      return Node::null();
                    }
                  }
                  Trace("strings-error") << "Looping term should be congruent : " << n << " " << eqc << " " << ncy << std::endl;
                  //should find a non-empty component, otherwise would have been singular congruent (I_Norm_S)
                  Assert(false);
                }else{
                  return ncy;
                }
              }else{
                if (d_im.hasProcessed())
                {
                  return Node::null();
                }
              }
            }
          }
        }
      }
      ++eqc_i;
    }
    curr.pop_back();
    //now we can add it to the list of equivalence classes
    d_strings_eqc.push_back( eqc );
  }else{
    //already processed
  }
  return Node::null();
}

void CoreSolver::checkNormalFormsEq()
{
  // calculate normal forms for each equivalence class, possibly adding
  // splitting lemmas
  d_normal_form.clear();
  std::map<Node, Node> nf_to_eqc;
  std::map<Node, Node> eqc_to_nf;
  std::map<Node, Node> eqc_to_exp;
  for (const Node& eqc : d_strings_eqc)
  {
    TypeNode stype = eqc.getType();
    Trace("strings-process-debug") << "- Verify normal forms are the same for "
                                   << eqc << std::endl;
    normalizeEquivalenceClass(eqc, stype);
    Trace("strings-debug") << "Finished normalizing eqc..." << std::endl;
    if (d_im.hasProcessed())
    {
      return;
    }
    NormalForm& nfe = getNormalForm(eqc);
    Node nf_term = utils::mkNConcat(nfe.d_nf, stype);
    std::map<Node, Node>::iterator itn = nf_to_eqc.find(nf_term);
    if (itn != nf_to_eqc.end())
    {
      NormalForm& nfe_eq = getNormalForm(itn->second);
      // two equivalence classes have same normal form, merge
      std::vector<Node> nf_exp;
      nf_exp.push_back(utils::mkAnd(nfe.d_exp));
      nf_exp.push_back(eqc_to_exp[itn->second]);
      Node eq = nfe.d_base.eqNode(nfe_eq.d_base);
      d_im.sendInference(nf_exp, eq, Inference::NORMAL_FORM);
      if (d_im.hasProcessed())
      {
        return;
      }
    }
    else
    {
      nf_to_eqc[nf_term] = eqc;
      eqc_to_nf[eqc] = nf_term;
      eqc_to_exp[eqc] = utils::mkAnd(nfe.d_exp);
    }
    Trace("strings-process-debug")
        << "Done verifying normal forms are the same for " << eqc << std::endl;
  }
  if (Trace.isOn("strings-nf"))
  {
    Trace("strings-nf") << "**** Normal forms are : " << std::endl;
    for (std::map<Node, Node>::iterator it = eqc_to_exp.begin();
         it != eqc_to_exp.end();
         ++it)
    {
      NormalForm& nf = getNormalForm(it->first);
      Trace("strings-nf") << "  N[" << it->first << "] (base " << nf.d_base
                          << ") = " << eqc_to_nf[it->first] << std::endl;
      Trace("strings-nf") << "     exp: " << it->second << std::endl;
    }
    Trace("strings-nf") << std::endl;
  }
}

//compute d_normal_forms_(base,exp,exp_depend)[eqc]
void CoreSolver::normalizeEquivalenceClass(Node eqc, TypeNode stype)
{
  Trace("strings-process-debug") << "Process equivalence class " << eqc << std::endl;
  Node emp = Word::mkEmptyWord(stype);
  if (d_state.areEqual(eqc, emp))
  {
#ifdef CVC4_ASSERTIONS
    for( unsigned j=0; j<d_eqc[eqc].size(); j++ ){
      Node n = d_eqc[eqc][j];
      for( unsigned i=0; i<n.getNumChildren(); i++ ){
        Assert(d_state.areEqual(n[i], emp));
      }
    }
#endif
    //do nothing
    Trace("strings-process-debug") << "Return process equivalence class " << eqc << " : empty." << std::endl;
    d_normal_form[eqc].init(emp);
  }
  else
  {
    // should not have computed the normal form of this equivalence class yet
    Assert(d_normal_form.find(eqc) == d_normal_form.end());
    // Normal forms for the relevant terms in the equivalence class of eqc
    std::vector<NormalForm> normal_forms;
    // map each term to its index in the above vector
    std::map<Node, unsigned> term_to_nf_index;
    // get the normal forms
    getNormalForms(eqc, normal_forms, term_to_nf_index, stype);
    if (d_im.hasProcessed())
    {
      return;
    }
    // process the normal forms
    processNEqc(normal_forms, stype);
    if (d_im.hasProcessed())
    {
      return;
    }

    //construct the normal form
    Assert(!normal_forms.empty());
    unsigned nf_index = 0;
    std::map<Node, unsigned>::iterator it = term_to_nf_index.find(eqc);
    // we prefer taking the normal form whose base is the equivalence
    // class representative, since this leads to shorter explanations in
    // some cases.
    if (it != term_to_nf_index.end())
    {
      nf_index = it->second;
    }
    d_normal_form[eqc] = normal_forms[nf_index];
    Trace("strings-process-debug")
        << "Return process equivalence class " << eqc
        << " : returned, size = " << d_normal_form[eqc].d_nf.size()
        << std::endl;
  }
}

NormalForm& CoreSolver::getNormalForm(Node n)
{
  std::map<Node, NormalForm>::iterator itn = d_normal_form.find(n);
  if (itn == d_normal_form.end())
  {
    Trace("strings-warn") << "WARNING: returning empty normal form for " << n
                          << std::endl;
    // Shouln't ask for normal forms of strings that weren't computed. This
    // likely means that n is not a representative or not a term in the current
    // context. We simply return a default normal form here in this case.
    Assert(false);
    return d_normal_form[n];
  }
  return itn->second;
}

Node CoreSolver::getNormalString(Node x, std::vector<Node>& nf_exp)
{
  if (!x.isConst())
  {
    Node xr = d_state.getRepresentative(x);
    TypeNode stype = xr.getType();
    std::map<Node, NormalForm>::iterator it = d_normal_form.find(xr);
    if (it != d_normal_form.end())
    {
      NormalForm& nf = it->second;
      Node ret = utils::mkNConcat(nf.d_nf, stype);
      nf_exp.insert(nf_exp.end(), nf.d_exp.begin(), nf.d_exp.end());
      d_im.addToExplanation(x, nf.d_base, nf_exp);
      Trace("strings-debug")
          << "Term: " << x << " has a normal form " << ret << std::endl;
      return ret;
    }
    // if x does not have a normal form, then it should not occur in the
    // equality engine and hence should be its own representative.
    Assert(xr == x);
    if (x.getKind() == kind::STRING_CONCAT)
    {
      std::vector<Node> vec_nodes;
      for (unsigned i = 0; i < x.getNumChildren(); i++)
      {
        Node nc = getNormalString(x[i], nf_exp);
        vec_nodes.push_back(nc);
      }
      return utils::mkNConcat(vec_nodes, stype);
    }
  }
  return x;
}

void CoreSolver::getNormalForms(Node eqc,
                                std::vector<NormalForm>& normal_forms,
                                std::map<Node, unsigned>& term_to_nf_index,
                                TypeNode stype)
{
  Node emp = Word::mkEmptyWord(stype);
  //constant for equivalence class
  Node eqc_non_c = eqc;
  Trace("strings-process-debug") << "Get normal forms " << eqc << std::endl;
  eq::EqualityEngine* ee = d_state.getEqualityEngine();
  eq::EqClassIterator eqc_i = eq::EqClassIterator( eqc, ee );
  while( !eqc_i.isFinished() ){
    Node n = (*eqc_i);
    if( !d_bsolver.isCongruent(n) ){
      if (n.isConst() || n.getKind() == STRING_CONCAT)
      {
        Trace("strings-process-debug") << "Get Normal Form : Process term " << n << " in eqc " << eqc << std::endl;
        NormalForm nf_curr;
        if (n.isConst())
        {
          nf_curr.init(n);
        }
        else if (n.getKind() == STRING_CONCAT)
        {
          // set the base to n, we construct the other portions of nf_curr in
          // the following.
          nf_curr.d_base = n;
          for( unsigned i=0; i<n.getNumChildren(); i++ ) {
            Node nr = ee->getRepresentative( n[i] );
            // get the normal form for the component
            NormalForm& nfr = getNormalForm(nr);
            std::vector<Node>& nfrv = nfr.d_nf;
            Trace("strings-process-debug") << "Normalizing subterm " << n[i] << " = "  << nr << std::endl;
            unsigned orig_size = nf_curr.d_nf.size();
            unsigned add_size = nfrv.size();
            //if not the empty string, add to current normal form
            if (!nfrv.empty())
            {
              // if in a build with assertions, we run the following block,
              // which checks that normal forms do not have concat terms.
              if (Configuration::isAssertionBuild())
              {
                for (const Node& nn : nfrv)
                {
                  if (Trace.isOn("strings-error"))
                  {
                    if (nn.getKind() == STRING_CONCAT)
                    {
                      Trace("strings-error")
                          << "Strings::Error: From eqc = " << eqc << ", " << n
                          << " index " << i << ", bad normal form : ";
                      for (unsigned rr = 0; rr < nfrv.size(); rr++)
                      {
                        Trace("strings-error") << nfrv[rr] << " ";
                      }
                      Trace("strings-error") << std::endl;
                    }
                  }
                  Assert(nn.getKind() != kind::STRING_CONCAT);
                }
              }
              nf_curr.d_nf.insert(nf_curr.d_nf.end(), nfrv.begin(), nfrv.end());
            }
            // Track explanation for the normal form. This is in two parts.
            // First, we must carry the explanation of the normal form computed
            // for the representative nr.
            for (const Node& exp : nfr.d_exp)
            {
              // The explanation is only relevant for the subsegment it was
              // previously relevant for, shifted now based on its relative
              // placement in the normal form of n.
              nf_curr.addToExplanation(
                  exp,
                  orig_size + nfr.d_expDep[exp][false],
                  orig_size + (add_size - nfr.d_expDep[exp][true]));
            }
            // Second, must explain that the component n[i] is equal to the
            // base of the normal form for nr.
            Node base = nfr.d_base;
            if (base != n[i])
            {
              Node eq = n[i].eqNode(base);
              // The equality is relevant for the entire current segment
              nf_curr.addToExplanation(eq, orig_size, orig_size + add_size);
            }
          }
          // Now that we are finished with the loop, we convert forward indices
          // to reverse indices in the explanation dependency information
          int total_size = nf_curr.d_nf.size();
          for (std::pair<const Node, std::map<bool, unsigned> >& ed :
               nf_curr.d_expDep)
          {
            ed.second[true] = total_size - ed.second[true];
            Assert(ed.second[true] >= 0);
          }
        }
        //if not equal to self
        std::vector<Node>& currv = nf_curr.d_nf;
        if (currv.size() > 1 || (currv.size() == 1 && currv[0].isConst()))
        {
          // if in a build with assertions, check that normal form is acyclic
          if (Configuration::isAssertionBuild())
          {
            if (currv.size() > 1)
            {
              for (unsigned i = 0; i < currv.size(); i++)
              {
                if (Trace.isOn("strings-error"))
                {
                  Trace("strings-error") << "Cycle for normal form ";
                  utils::printConcatTrace(currv, "strings-error");
                  Trace("strings-error") << "..." << currv[i] << std::endl;
                }
                Assert(!d_state.areEqual(currv[i], n));
              }
            }
          }
          term_to_nf_index[n] = normal_forms.size();
          normal_forms.push_back(nf_curr);
        }else{
          //this was redundant: combination of self + empty string(s)
          Node nn = currv.size() == 0 ? emp : currv[0];
          Assert(d_state.areEqual(nn, eqc));
        }
      }else{
        eqc_non_c = n;
      }
    }
    ++eqc_i;
  }

  if( normal_forms.empty() ) {
    Trace("strings-solve-debug2") << "construct the normal form" << std::endl;
    // This case happens when there are no non-trivial normal forms for this
    // equivalence class. For example, given assertions:
    //   { x = y ++ z, x = y, z = "" }
    // The equivalence class of { x, y, y ++ z } is such that the normal form
    // of all terms is a variable (either x or y) in the equivalence class
    // itself. Thus, the normal form of this equivalence class can be assigned
    // to one of these variables.
    // We use a non-concatenation term among the terms in this equivalence
    // class, which is stored in eqc_non_c. The reason is this does not require
    // an explanation, whereas e.g. y ++ z would require the explanation z = ""
    // to justify its normal form is y.
    Assert(eqc_non_c.getKind() != STRING_CONCAT);
    NormalForm nf_triv;
    nf_triv.init(eqc_non_c);
    normal_forms.push_back(nf_triv);
  }else{
    if(Trace.isOn("strings-solve")) {
      Trace("strings-solve") << "--- Normal forms for equivalance class " << eqc << " : " << std::endl;
      for (unsigned i = 0, size = normal_forms.size(); i < size; i++)
      {
        NormalForm& nf = normal_forms[i];
        Trace("strings-solve") << "#" << i << " (from " << nf.d_base << ") : ";
        for (unsigned j = 0, sizej = nf.d_nf.size(); j < sizej; j++)
        {
          if(j>0) {
            Trace("strings-solve") << ", ";
          }
          Trace("strings-solve") << nf.d_nf[j];
        }
        Trace("strings-solve") << std::endl;
        Trace("strings-solve") << "   Explanation is : ";
        if (nf.d_exp.size() == 0)
        {
          Trace("strings-solve") << "NONE";
        } else {
          for (unsigned j = 0, sizej = nf.d_exp.size(); j < sizej; j++)
          {
            if(j>0) {
              Trace("strings-solve") << " AND ";
            }
            Trace("strings-solve") << nf.d_exp[j];
          }
          Trace("strings-solve") << std::endl;
          Trace("strings-solve") << "WITH DEPENDENCIES : " << std::endl;
          for (unsigned j = 0, sizej = nf.d_exp.size(); j < sizej; j++)
          {
            Node exp = nf.d_exp[j];
            Trace("strings-solve") << "   " << exp << " -> ";
            Trace("strings-solve") << nf.d_expDep[exp][false] << ",";
            Trace("strings-solve") << nf.d_expDep[exp][true] << std::endl;
          }
        }
        Trace("strings-solve") << std::endl;
        
      }
    } else {
      Trace("strings-solve") << "--- Single normal form for equivalence class " << eqc << std::endl;
    }
    
    //if equivalence class is constant, approximate as containment, infer conflicts
    Node c = d_bsolver.getConstantEqc( eqc );
    if( !c.isNull() ){
      Trace("strings-solve") << "Eqc is constant " << c << std::endl;
      for (unsigned i = 0, size = normal_forms.size(); i < size; i++)
      {
        NormalForm& nf = normal_forms[i];
        int firstc, lastc;
        if (!StringsEntail::canConstantContainList(c, nf.d_nf, firstc, lastc))
        {
          Node n = nf.d_base;
          //conflict
          Trace("strings-solve") << "Normal form for " << n << " cannot be contained in constant " << c << std::endl;
          //conflict, explanation is n = base ^ base = c ^ relevant porition of ( n = N[n] )
          std::vector< Node > exp;
          d_bsolver.explainConstantEqc(n,eqc,exp);
          // Notice although not implemented, this can be minimized based on
          // firstc/lastc, normal_forms_exp_depend.
          exp.insert(exp.end(), nf.d_exp.begin(), nf.d_exp.end());
          Node conc = d_false;
          d_im.sendInference(exp, conc, Inference::N_NCTN);
        }
      }
    }
  }
}

void CoreSolver::processNEqc(std::vector<NormalForm>& normal_forms,
                             TypeNode stype)
{
  //the possible inferences
  std::vector<CoreInferInfo> pinfer;
  // loop over all pairs 
  for(unsigned i=0; i<normal_forms.size()-1; i++) {
    //unify each normalform[j] with normal_forms[i]
    for(unsigned j=i+1; j<normal_forms.size(); j++ ) {
      NormalForm& nfi = normal_forms[i];
      NormalForm& nfj = normal_forms[j];
      //ensure that normal_forms[i] and normal_forms[j] are the same modulo equality, add to pinfer if not
      Trace("strings-solve") << "Strings: Process normal form #" << i << " against #" << j << "..." << std::endl;
      if (isNormalFormPair(nfi.d_base, nfj.d_base))
      {
        Trace("strings-solve") << "Strings: Already cached." << std::endl;
      }else{
        //process the reverse direction first (check for easy conflicts and inferences)
        unsigned rindex = 0;
        nfi.reverse();
        nfj.reverse();
        processSimpleNEq(nfi, nfj, rindex, true, 0, pinfer, stype);
        nfi.reverse();
        nfj.reverse();
        if (d_im.hasProcessed())
        {
          return;
        }
        //AJR: for less aggressive endpoint inference
        //rindex = 0;

        unsigned index = 0;
        processSimpleNEq(nfi, nfj, index, false, rindex, pinfer, stype);
        if (d_im.hasProcessed())
        {
          return;
        }
      }
    }
  }
  if (pinfer.empty())
  {
    return;
  }
  // now, determine which of the possible inferences we want to add
  unsigned use_index = 0;
  bool set_use_index = false;
  Trace("strings-solve") << "Possible inferences (" << pinfer.size()
                         << ") : " << std::endl;
  Inference min_id = Inference::NONE;
  unsigned max_index = 0;
  for (unsigned i = 0, psize = pinfer.size(); i < psize; i++)
  {
    CoreInferInfo& ipii = pinfer[i];
    InferInfo& ii = ipii.d_infer;
    Trace("strings-solve") << "#" << i << ": From " << ipii.d_i << " / "
                           << ipii.d_j << " (rev=" << ipii.d_rev << ") : ";
    Trace("strings-solve") << ii.d_conc << " by " << ii.d_id << std::endl;
    if (!set_use_index || ii.d_id < min_id
        || (ii.d_id == min_id && ipii.d_index > max_index))
    {
      min_id = ii.d_id;
      max_index = ipii.d_index;
      use_index = i;
      set_use_index = true;
    }
  }
  Trace("strings-solve") << "...choose #" << use_index << std::endl;
  if (!processInferInfo(pinfer[use_index]))
  {
    Unhandled() << "Failed to process infer info " << pinfer[use_index].d_infer
                << std::endl;
  }
}

void CoreSolver::processSimpleNEq(NormalForm& nfi,
                                  NormalForm& nfj,
                                  unsigned& index,
                                  bool isRev,
                                  unsigned rproc,
                                  std::vector<CoreInferInfo>& pinfer,
                                  TypeNode stype)
{
  NodeManager* nm = NodeManager::currentNM();
  Node emp = Word::mkEmptyWord(stype);

  const std::vector<Node>& nfiv = nfi.d_nf;
  const std::vector<Node>& nfjv = nfj.d_nf;
  Assert(rproc <= nfiv.size() && rproc <= nfjv.size());
  while (true)
  {
    bool lhsDone = (index == (nfiv.size() - rproc));
    bool rhsDone = (index == (nfjv.size() - rproc));
    if (lhsDone && rhsDone)
    {
      // We are done with both normal forms
      break;
    }
    else if (lhsDone || rhsDone)
    {
      // Only one side is done so the remainder of the other side must be empty
      NormalForm& nfk = index == (nfiv.size() - rproc) ? nfj : nfi;
      std::vector<Node>& nfkv = nfk.d_nf;
      unsigned index_k = index;
      std::vector<Node> curr_exp;
      NormalForm::getExplanationForPrefixEq(nfi, nfj, -1, -1, curr_exp);
      while (!d_state.isInConflict() && index_k < (nfkv.size() - rproc))
      {
        // can infer that this string must be empty
        Node eq = nfkv[index_k].eqNode(emp);
        Assert(!d_state.areEqual(emp, nfkv[index_k]));
        d_im.sendInference(curr_exp, eq, Inference::N_ENDPOINT_EMP);
        index_k++;
      }
      break;
    }

    // We have inferred that the normal forms up to position `index` are
    // equivalent. Below, we refer to the components at the current position of
    // the normal form as `x` and `y`.
    //
    // E.g. x ++ ... = y ++ ...
    Node x = nfiv[index];
    Node y = nfjv[index];
    Trace("strings-solve-debug")
        << "Process " << x << " ... " << y << std::endl;

    if (x == y)
    {
      // The normal forms have the same term at the current position. We just
      // continue with the next index. By construction of the normal forms, we
      // end up in this case if the two components are equal according to the
      // equality engine (i.e. we cannot have different x and y terms that are
      // equal in the equality engine).
      //
      // E.g. x ++ x' ++ ... = x ++ y' ++ ... ---> x' ++ ... = y' ++ ...
      Trace("strings-solve-debug")
          << "Simple Case 1 : strings are equal" << std::endl;
      index++;
      continue;
    }
    Assert(!d_state.areEqual(x, y));

    std::vector<Node> lenExp;
    Node xLenTerm = d_state.getLength(x, lenExp);
    Node yLenTerm = d_state.getLength(y, lenExp);

    if (d_state.areEqual(xLenTerm, yLenTerm))
    {
      // `x` and `y` have the same length. We infer that the two components
      // have to be the same.
      //
      // E.g. x ++ ... = y ++ ... ^ len(x) = len(y) ---> x = y
      Trace("strings-solve-debug")
          << "Simple Case 2 : string lengths are equal" << std::endl;
      Node eq = x.eqNode(y);
      Node leneq = xLenTerm.eqNode(yLenTerm);
      NormalForm::getExplanationForPrefixEq(nfi, nfj, index, index, lenExp);
      lenExp.push_back(leneq);
      d_im.sendInference(lenExp, eq, Inference::N_UNIFY);
      break;
    }
    else if ((!x.isConst() && index == nfiv.size() - rproc - 1)
             || (!y.isConst() && index == nfjv.size() - rproc - 1))
    {
      // We have reached the last component in one of the normal forms and it
      // is not a constant. Thus, the last component must be equal to the
      // remainder of the other normal form.
      //
      // E.g. x = y ++ y' ---> x = y ++ y'
      Trace("strings-solve-debug")
          << "Simple Case 3 : at endpoint" << std::endl;
      Node eqn[2];
      for (unsigned r = 0; r < 2; r++)
      {
        const NormalForm& nfk = r == 0 ? nfi : nfj;
        const std::vector<Node>& nfkv = nfk.d_nf;
        std::vector<Node> eqnc;
        for (unsigned i = index, size = (nfkv.size() - rproc); i < size; i++)
        {
          if (isRev)
          {
            eqnc.insert(eqnc.begin(), nfkv[i]);
          }
          else
          {
            eqnc.push_back(nfkv[i]);
          }
        }
        eqn[r] = utils::mkNConcat(eqnc, stype);
      }
      if (!d_state.areEqual(eqn[0], eqn[1]))
      {
        std::vector<Node> antec;
        NormalForm::getExplanationForPrefixEq(nfi, nfj, -1, -1, antec);
        d_im.sendInference(
            antec, eqn[0].eqNode(eqn[1]), Inference::N_ENDPOINT_EQ, true);
      }
      else
      {
        Assert(nfiv.size() == nfjv.size());
        index = nfiv.size() - rproc;
      }
      break;
    }
    else if (x.isConst() && y.isConst())
    {
      // Constants in both normal forms.
      //
      // E.g. "abc" ++ ... = "ab" ++ ...
      size_t lenX = Word::getLength(x);
      size_t lenY = Word::getLength(y);
      Trace("strings-solve-debug")
          << "Simple Case 4 : Const Split : " << x << " vs " << y
          << " at index " << index << ", isRev = " << isRev << std::endl;
      size_t minLen = std::min(lenX, lenY);
      bool isSameFix =
          isRev ? Word::rstrncmp(x, y, minLen) : Word::strncmp(x, y, minLen);
      if (isSameFix)
      {
        // The shorter constant is a prefix/suffix of the longer constant. We
        // split the longer constant into the shared part and the remainder and
        // continue from there.
        //
        // E.g. "abc" ++ x' ++ ... = "ab" ++ y' ++ ... --->
        //      "c" ++ x' ++ ... = y' ++ ...
        bool xShorter = lenX < lenY;
        NormalForm& nfl = xShorter ? nfj : nfi;
        Node cl = xShorter ? y : x;
        Node ns = xShorter ? x : y;

        Node remainderStr;
        if (isRev)
        {
          size_t newlen = std::max(lenX, lenY) - minLen;
          remainderStr = Word::substr(cl, 0, newlen);
        }
        else
        {
          remainderStr = Word::substr(cl, minLen);
        }
        Trace("strings-solve-debug-test")
            << "Break normal form of " << cl << " into " << ns << ", "
            << remainderStr << std::endl;
        nfl.splitConstant(index, ns, remainderStr);
        index++;
        continue;
      }
      else
      {
        // Conflict because the shorter constant is not a prefix/suffix of the
        // other.
        //
        // E.g. "abc" ++ ... = "bc" ++ ... ---> conflict
        std::vector<Node> antec;
        NormalForm::getExplanationForPrefixEq(nfi, nfj, index, index, antec);
        d_im.sendInference(antec, d_false, Inference::N_CONST, true);
        break;
      }
    }

    // The candidate inference "info"
    CoreInferInfo info;
    InferInfo& iinfo = info.d_infer;
    info.d_index = index;
    // for debugging
    info.d_i = nfi.d_base;
    info.d_j = nfj.d_base;
    info.d_rev = isRev;
    Assert(index < nfiv.size() - rproc && index < nfjv.size() - rproc);
    if (!d_state.areDisequal(xLenTerm, yLenTerm) && !d_state.areEqual(xLenTerm, yLenTerm)
        && !x.isConst()
        && !y.isConst())  // AJR: remove the latter 2 conditions?
    {
      // We don't know whether `x` and `y` have the same length or not. We
      // split on whether they are equal or not (note that splitting on
      // equality between strings is worse since it is harder to process).
      //
      // E.g. x ++ ... = y ++ ... ---> (len(x) = len(y)) v (len(x) != len(y))
      Trace("strings-solve-debug")
          << "Non-simple Case 1 : string lengths neither equal nor disequal"
          << std::endl;
      Node lenEq = nm->mkNode(EQUAL, xLenTerm, yLenTerm);
      lenEq = Rewriter::rewrite(lenEq);
      iinfo.d_conc = nm->mkNode(OR, lenEq, lenEq.negate());
      iinfo.d_id = Inference::LEN_SPLIT;
      info.d_pendingPhase[lenEq] = true;
      pinfer.push_back(info);
      break;
    }

    Trace("strings-solve-debug")
        << "Non-simple Case 2 : must compare strings" << std::endl;
    int lhsLoopIdx = -1;
    int rhsLoopIdx = -1;
    if (detectLoop(nfi, nfj, index, lhsLoopIdx, rhsLoopIdx, rproc))
    {
      // We are dealing with a looping word equation.
      if (!isRev)
      {  // FIXME
        NormalForm::getExplanationForPrefixEq(nfi, nfj, -1, -1, iinfo.d_ant);
        ProcessLoopResult plr =
            processLoop(lhsLoopIdx != -1 ? nfi : nfj,
                        lhsLoopIdx != -1 ? nfj : nfi,
                        lhsLoopIdx != -1 ? lhsLoopIdx : rhsLoopIdx,
                        index,
                        info);
        if (plr == ProcessLoopResult::INFERENCE)
        {
          pinfer.push_back(info);
          break;
        }
        else if (plr == ProcessLoopResult::CONFLICT)
        {
          break;
        }
        Assert(plr == ProcessLoopResult::SKIPPED);
      }
    }

    // AJR: length entailment here?
    if (x.isConst() || y.isConst())
    {
      // Below, we deal with the case where `x` or `y` is a constant string. We
      // refer to the non-constant component as `nc` below.
      //
      // E.g. "abc" ++ ... = nc ++ ...
      Assert(!x.isConst() || !y.isConst());
      NormalForm& nfc = x.isConst() ? nfi : nfj;
      const std::vector<Node>& nfcv = nfc.d_nf;
      NormalForm& nfnc = x.isConst() ? nfj : nfi;
      const std::vector<Node>& nfncv = nfnc.d_nf;
      Node nc = nfncv[index];
      Assert(!nc.isConst()) << "Other string is not constant.";
      Assert(nc.getKind() != STRING_CONCAT) << "Other string is not CONCAT.";
      // explanation for why nc is non-empty
      Node expNonEmpty = d_state.explainNonEmpty(nc);
      if (expNonEmpty.isNull())
      {
        // The non-constant side may be equal to the empty string. Split on
        // whether it is.
        //
        // E.g. "abc" ++ ... = nc ++ ... ---> (nc = "") v (nc != "")
        Node eq = nc.eqNode(emp);
        eq = Rewriter::rewrite(eq);
        if (eq.isConst())
        {
          // If the equality rewrites to a constant, we must use the
          // purify variable for this string to communicate that
          // we have inferred whether it is empty.
          SkolemCache* skc = d_termReg.getSkolemCache();
          Node p = skc->mkSkolemCached(nc, SkolemCache::SK_PURIFY, "lsym");
          Node pEq = p.eqNode(emp);
          // should not be constant
          Assert(!Rewriter::rewrite(pEq).isConst());
          // infer the purification equality, and the (dis)equality
          // with the empty string in the direction that the rewriter
          // inferred
          iinfo.d_conc = nm->mkNode(
              AND, p.eqNode(nc), !eq.getConst<bool>() ? pEq.negate() : pEq);
          iinfo.d_id = Inference::INFER_EMP;
        }
        else
        {
          iinfo.d_conc = nm->mkNode(OR, eq, eq.negate());
          iinfo.d_id = Inference::LEN_SPLIT_EMP;
        }
        pinfer.push_back(info);
        break;
      }

      // At this point, we know that `nc` is non-empty, so we add that to our
      // explanation.
      iinfo.d_ant.push_back(expNonEmpty);

      size_t ncIndex = index + 1;
      Node nextConstStr = nfnc.collectConstantStringAt(ncIndex);
      if (!nextConstStr.isNull())
      {
        // We are in the case where we have a constant after `nc`, so we
        // split `nc`.
        //
        // E.g. "abc" ++ ... = nc ++ "b" ++ ... ---> nc = "a" ++ k
        size_t cIndex = index;
        Node stra = nfc.collectConstantStringAt(cIndex);
        size_t straLen = Word::getLength(stra);
        Assert(!stra.isNull());
        Node strb = nextConstStr;
        // Since `nc` is non-empty, we start with character 1
        size_t p;
        if (isRev)
        {
          Node stra1 = Word::prefix(stra, straLen - 1);
          p = straLen - Word::roverlap(stra1, strb);
          Trace("strings-csp-debug")
              << "Compute roverlap : " << stra1 << " " << strb << std::endl;
          size_t p2 = Word::rfind(stra1, strb);
          p = p2 == std::string::npos ? p : (p > p2 + 1 ? p2 + 1 : p);
          Trace("strings-csp-debug")
              << "roverlap : " << stra1 << " " << strb << " returned " << p
              << " " << p2 << " " << (p2 == std::string::npos) << std::endl;
        }
        else
        {
          Node stra1 = Word::substr(stra, 1);
          p = straLen - Word::overlap(stra1, strb);
          Trace("strings-csp-debug")
              << "Compute overlap : " << stra1 << " " << strb << std::endl;
          size_t p2 = Word::find(stra1, strb);
          p = p2 == std::string::npos ? p : (p > p2 + 1 ? p2 + 1 : p);
          Trace("strings-csp-debug")
              << "overlap : " << stra1 << " " << strb << " returned " << p
              << " " << p2 << " " << (p2 == std::string::npos) << std::endl;
        }

        // If we can't split off more than a single character from the
        // constant, we might as well do regular constant/non-constant
        // inference (see below).
        if (p > 1)
        {
          NormalForm::getExplanationForPrefixEq(
              nfc, nfnc, cIndex, ncIndex, iinfo.d_ant);
          Node prea = p == straLen ? stra
                                   : (isRev ? Word::suffix(stra, p)
                                            : Word::prefix(stra, p));
          SkolemCache* skc = d_termReg.getSkolemCache();
          Node sk = skc->mkSkolemCached(
              nc,
              prea,
              isRev ? SkolemCache::SK_ID_C_SPT_REV : SkolemCache::SK_ID_C_SPT,
              "c_spt");
          Trace("strings-csp")
              << "Const Split: " << prea << " is removed from " << stra
              << " due to " << strb << ", p=" << p << std::endl;
          iinfo.d_conc = nc.eqNode(isRev ? utils::mkNConcat(sk, prea)
                                         : utils::mkNConcat(prea, sk));
          iinfo.d_new_skolem[LENGTH_SPLIT].push_back(sk);
          iinfo.d_id = Inference::SSPLIT_CST_PROP;
          pinfer.push_back(info);
          break;
        }
      }

      // Since none of the other inferences apply, we just infer that `nc` has
      // to start with the first character of the constant.
      //
      // E.g. "abc" ++ ... = nc ++ ... ---> nc = "a" ++ k
      Node stra = nfcv[index];
      size_t straLen = Word::getLength(stra);
      Node firstChar = straLen == 1 ? stra
                                    : (isRev ? Word::suffix(stra, 1)
                                             : Word::prefix(stra, 1));
      SkolemCache* skc = d_termReg.getSkolemCache();
      Node sk = skc->mkSkolemCached(
          nc,
          isRev ? SkolemCache::SK_ID_VC_SPT_REV : SkolemCache::SK_ID_VC_SPT,
          "c_spt");
      Trace("strings-csp") << "Const Split: " << firstChar
                           << " is removed from " << stra << " (serial) "
                           << std::endl;
      NormalForm::getExplanationForPrefixEq(
          nfi, nfj, index, index, iinfo.d_ant);
      iinfo.d_conc = nc.eqNode(isRev ? utils::mkNConcat(sk, firstChar)
                                     : utils::mkNConcat(firstChar, sk));
      iinfo.d_new_skolem[LENGTH_SPLIT].push_back(sk);
      iinfo.d_id = Inference::SSPLIT_CST;
      pinfer.push_back(info);
      break;
    }

    // Below, we deal with the case where `x` and `y` are two non-constant
    // terms of different lengths. In this case, we have to split on which term
    // is a prefix/suffix of the other.
    //
    // E.g. x ++ ... = y ++ ... ---> (x = y ++ k) v (y = x ++ k)
    Assert(d_state.areDisequal(xLenTerm, yLenTerm));
    Assert(!x.isConst());
    Assert(!y.isConst());

    int32_t lentTestSuccess = -1;
    Node lentTestExp;
    if (options::stringCheckEntailLen())
    {
      // If length entailment checks are enabled, we can save the case split by
      // inferring that `x` has to be longer than `y` or vice-versa.
      for (size_t e = 0; e < 2; e++)
      {
        Node t = e == 0 ? x : y;
        // do not infer constants are larger than variables
        if (!t.isConst())
        {
          Node lt1 = e == 0 ? xLenTerm : yLenTerm;
          Node lt2 = e == 0 ? yLenTerm : xLenTerm;
          Node entLit = Rewriter::rewrite(nm->mkNode(GT, lt1, lt2));
          std::pair<bool, Node> et = d_state.entailmentCheck(
              options::TheoryOfMode::THEORY_OF_TYPE_BASED, entLit);
          if (et.first)
          {
            Trace("strings-entail")
                << "Strings entailment : " << entLit
                << " is entailed in the current context." << std::endl;
            Trace("strings-entail")
                << "  explanation was : " << et.second << std::endl;
            lentTestSuccess = e;
            lentTestExp = et.second;
            break;
          }
        }
      }
    }

    NormalForm::getExplanationForPrefixEq(nfi, nfj, index, index, iinfo.d_ant);
    // Add premises for x != "" ^ y != ""
    for (unsigned xory = 0; xory < 2; xory++)
    {
      Node t = xory == 0 ? x : y;
      Node tnz = d_state.explainNonEmpty(x);
      if (!tnz.isNull())
      {
        iinfo.d_ant.push_back(tnz);
      }
      else
      {
        tnz = x.eqNode(emp).negate();
        iinfo.d_antn.push_back(tnz);
      }
    }
    SkolemCache* skc = d_termReg.getSkolemCache();
    Node sk = skc->mkSkolemCached(
        x,
        y,
        isRev ? SkolemCache::SK_ID_V_SPT_REV : SkolemCache::SK_ID_V_SPT,
        "v_spt");
    iinfo.d_new_skolem[LENGTH_GEQ_ONE].push_back(sk);
    Node eq1 =
        x.eqNode(isRev ? utils::mkNConcat(sk, y) : utils::mkNConcat(y, sk));
    Node eq2 =
        y.eqNode(isRev ? utils::mkNConcat(sk, x) : utils::mkNConcat(x, sk));

    if (lentTestSuccess != -1)
    {
      iinfo.d_antn.push_back(lentTestExp);
      iinfo.d_conc = lentTestSuccess == 0 ? eq1 : eq2;
      iinfo.d_id = Inference::SSPLIT_VAR_PROP;
    }
    else
    {
      Node ldeq = nm->mkNode(EQUAL, xLenTerm, yLenTerm).negate();
      iinfo.d_ant.push_back(ldeq);
      iinfo.d_conc = nm->mkNode(OR, eq1, eq2);
      iinfo.d_id = Inference::SSPLIT_VAR;
    }
    pinfer.push_back(info);
    break;
  }
}

bool CoreSolver::detectLoop(NormalForm& nfi,
                               NormalForm& nfj,
                               int index,
                               int& loop_in_i,
                               int& loop_in_j,
                               unsigned rproc)
{
  int has_loop[2] = { -1, -1 };
  for (unsigned r = 0; r < 2; r++)
  {
    NormalForm& nf = r == 0 ? nfi : nfj;
    NormalForm& nfo = r == 0 ? nfj : nfi;
    std::vector<Node>& nfv = nf.d_nf;
    std::vector<Node>& nfov = nfo.d_nf;
    if (nfov[index].isConst())
    {
      continue;
    }
    for (unsigned lp = index + 1, lpEnd = nfv.size() - rproc; lp < lpEnd; lp++)
    {
      if (nfv[lp] == nfov[index])
      {
        has_loop[r] = lp;
        break;
      }
    }
  }
  if( has_loop[0]!=-1 || has_loop[1]!=-1 ) {
    loop_in_i = has_loop[0];
    loop_in_j = has_loop[1];
    return true;
  } else {
    Trace("strings-solve-debug") << "No loops detected." << std::endl;
    return false;
  }
}

//xs(zy)=t(yz)xr
CoreSolver::ProcessLoopResult CoreSolver::processLoop(NormalForm& nfi,
                                                      NormalForm& nfj,
                                                      int loop_index,
                                                      int index,
                                                      CoreInferInfo& info)
{
  if (options::stringProcessLoopMode() == options::ProcessLoopMode::ABORT)
  {
    throw LogicException("Looping word equation encountered.");
  }
  else if (options::stringProcessLoopMode() == options::ProcessLoopMode::NONE)
  {
    d_im.setIncomplete();
    return ProcessLoopResult::SKIPPED;
  }

  NodeManager* nm = NodeManager::currentNM();
  Node conc;
  const std::vector<Node>& veci = nfi.d_nf;
  const std::vector<Node>& vecoi = nfj.d_nf;

  TypeNode stype = veci[loop_index].getType();

  Trace("strings-loop") << "Detected possible loop for " << veci[loop_index]
                        << std::endl;
  Trace("strings-loop") << " ... (X)= " << vecoi[index] << std::endl;
  Trace("strings-loop") << " ... T(Y.Z)= ";
  std::vector<Node> vec_t(veci.begin() + index, veci.begin() + loop_index);
  Node t_yz = utils::mkNConcat(vec_t, stype);
  Trace("strings-loop") << " (" << t_yz << ")" << std::endl;
  Trace("strings-loop") << " ... S(Z.Y)= ";
  std::vector<Node> vec_s(vecoi.begin() + index + 1, vecoi.end());
  Node s_zy = utils::mkNConcat(vec_s, stype);
  Trace("strings-loop") << s_zy << std::endl;
  Trace("strings-loop") << " ... R= ";
  std::vector<Node> vec_r(veci.begin() + loop_index + 1, veci.end());
  Node r = utils::mkNConcat(vec_r, stype);
  Trace("strings-loop") << r << std::endl;

  Node emp = Word::mkEmptyWord(stype);

  InferInfo& iinfo = info.d_infer;
  if (s_zy.isConst() && r.isConst() && r != emp)
  {
    int c;
    bool flag = true;
    if (s_zy.getConst<String>().tailcmp(r.getConst<String>(), c))
    {
      if (c >= 0)
      {
        s_zy = Word::substr(s_zy, 0, c);
        r = emp;
        vec_r.clear();
        Trace("strings-loop") << "Strings::Loop: Refactor S(Z.Y)= " << s_zy
                              << ", c=" << c << std::endl;
        flag = false;
      }
    }
    if (flag)
    {
      Trace("strings-loop") << "Strings::Loop: tails are different."
                            << std::endl;
      d_im.sendInference(iinfo.d_ant, conc, Inference::FLOOP_CONFLICT, true);
      return ProcessLoopResult::CONFLICT;
    }
  }

  Node split_eq;
  for (unsigned i = 0; i < 2; i++)
  {
    Node t = i == 0 ? veci[loop_index] : t_yz;
    split_eq = t.eqNode(emp);
    Node split_eqr = Rewriter::rewrite(split_eq);
    // the equality could rewrite to false
    if (!split_eqr.isConst())
    {
      Node expNonEmpty = d_state.explainNonEmpty(t);
      if (expNonEmpty.isNull())
      {
        // try to make t equal to empty to avoid loop
        iinfo.d_conc = nm->mkNode(kind::OR, split_eq, split_eq.negate());
        iinfo.d_id = Inference::LEN_SPLIT_EMP;
        return ProcessLoopResult::INFERENCE;
      }
      else
      {
        iinfo.d_ant.push_back(expNonEmpty);
      }
    }
    else
    {
      Assert(!split_eqr.getConst<bool>());
    }
  }

  Node ant = d_im.mkExplain(iinfo.d_ant);
  iinfo.d_ant.clear();
  iinfo.d_antn.push_back(ant);

  Node str_in_re;
  if (s_zy == t_yz && r == emp && s_zy.isConst()
      && s_zy.getConst<String>().isRepeated())
  {
    Node rep_c = Word::substr(s_zy, 0, 1);
    Trace("strings-loop") << "Special case (X)=" << vecoi[index] << " "
                          << std::endl;
    Trace("strings-loop") << "... (C)=" << rep_c << " " << std::endl;
    // special case
    str_in_re = nm->mkNode(
        STRING_IN_REGEXP,
        vecoi[index],
        nm->mkNode(REGEXP_STAR, nm->mkNode(STRING_TO_REGEXP, rep_c)));
    conc = str_in_re;
  }
  else if (t_yz.isConst())
  {
    Trace("strings-loop") << "Strings::Loop: Const Normal Breaking."
                          << std::endl;
    unsigned size = Word::getLength(t_yz);
    std::vector<Node> vconc;
    for (unsigned len = 1; len <= size; len++)
    {
      Node y = Word::substr(t_yz, 0, len);
      Node z = Word::substr(t_yz, len, size - len);
      Node restr = s_zy;
      Node cc;
      if (r != emp)
      {
        std::vector<Node> v2(vec_r);
        v2.insert(v2.begin(), y);
        v2.insert(v2.begin(), z);
        restr = utils::mkNConcat(z, y);
        cc = Rewriter::rewrite(s_zy.eqNode(utils::mkNConcat(v2, stype)));
      }
      else
      {
        cc = Rewriter::rewrite(s_zy.eqNode(utils::mkNConcat(z, y)));
      }
      if (cc == d_false)
      {
        continue;
      }
      Node conc2 = nm->mkNode(
          STRING_IN_REGEXP,
          vecoi[index],
          nm->mkNode(
              REGEXP_CONCAT,
              nm->mkNode(STRING_TO_REGEXP, y),
              nm->mkNode(REGEXP_STAR, nm->mkNode(STRING_TO_REGEXP, restr))));
      cc = cc == d_true ? conc2 : nm->mkNode(kind::AND, cc, conc2);
      vconc.push_back(cc);
    }
    conc = vconc.size() == 0 ? Node::null() : vconc.size() == 1
                                                  ? vconc[0]
                                                  : nm->mkNode(kind::OR, vconc);
  }
  else
  {
    if (options::stringProcessLoopMode()
        == options::ProcessLoopMode::SIMPLE_ABORT)
    {
      throw LogicException("Normal looping word equation encountered.");
    }
    else if (options::stringProcessLoopMode()
             == options::ProcessLoopMode::SIMPLE)
    {
      d_im.setIncomplete();
      return ProcessLoopResult::SKIPPED;
    }

    Trace("strings-loop") << "Strings::Loop: Normal Loop Breaking."
                          << std::endl;
    // right
    SkolemCache* skc = d_termReg.getSkolemCache();
    Node sk_w = skc->mkSkolem("w_loop");
    Node sk_y = skc->mkSkolem("y_loop");
    d_termReg.registerTermAtomic(sk_y, LENGTH_GEQ_ONE);
    Node sk_z = skc->mkSkolem("z_loop");
    // t1 * ... * tn = y * z
    Node conc1 = t_yz.eqNode(utils::mkNConcat(sk_y, sk_z));
    // s1 * ... * sk = z * y * r
    vec_r.insert(vec_r.begin(), sk_y);
    vec_r.insert(vec_r.begin(), sk_z);
    Node conc2 = s_zy.eqNode(utils::mkNConcat(vec_r, stype));
    Node conc3 = vecoi[index].eqNode(utils::mkNConcat(sk_y, sk_w));
    Node restr = r == emp ? s_zy : utils::mkNConcat(sk_z, sk_y);
    str_in_re =
        nm->mkNode(kind::STRING_IN_REGEXP,
                   sk_w,
                   nm->mkNode(kind::REGEXP_STAR,
                              nm->mkNode(kind::STRING_TO_REGEXP, restr)));

    std::vector<Node> vec_conc;
    vec_conc.push_back(conc1);
    vec_conc.push_back(conc2);
    vec_conc.push_back(conc3);
    vec_conc.push_back(str_in_re);
    conc = nm->mkNode(kind::AND, vec_conc);
  }  // normal case

  // we will be done
  iinfo.d_conc = conc;
  iinfo.d_id = Inference::FLOOP;
  info.d_nfPair[0] = nfi.d_base;
  info.d_nfPair[1] = nfj.d_base;
  return ProcessLoopResult::INFERENCE;
}

void CoreSolver::processDeq(Node ni, Node nj)
{
  NodeManager* nm = NodeManager::currentNM();
  NormalForm& nfni = getNormalForm(ni);
  NormalForm& nfnj = getNormalForm(nj);

  if (nfni.d_nf.size() <= 1 && nfnj.d_nf.size() <= 1)
  {
    return;
  }

  std::vector<Node> nfi = nfni.d_nf;
  std::vector<Node> nfj = nfnj.d_nf;

  // See if one side is constant, if so, the disequality ni != nj is satisfied
  // if it cannot contain the other side.
  //
  // E.g. "abc" != x ++ "d" ++ y
  for (uint32_t i = 0; i < 2; i++)
  {
    Node c = d_bsolver.getConstantEqc(i == 0 ? ni : nj);
    if (!c.isNull())
    {
      int findex, lindex;
      if (!StringsEntail::canConstantContainList(
              c, i == 0 ? nfj : nfi, findex, lindex))
      {
        Trace("strings-solve-debug")
            << "Disequality: constant cannot contain list" << std::endl;
        return;
      }
    }
  }

  if (processReverseDeq(nfi, nfj, ni, nj))
  {
    return;
  }

  nfi = nfni.d_nf;
  nfj = nfnj.d_nf;

  size_t index = 0;
  while (index < nfi.size() || index < nfj.size())
  {
    if (processSimpleDeq(nfi, nfj, ni, nj, index, false))
    {
      return;
    }

    // We have inferred that the normal forms up to position `index` are
    // equivalent. Below, we refer to the components at the current position of
    // the normal form as `x` and `y`.
    //
    // E.g. x ++ ... = y ++ ...
    Assert(index < nfi.size() && index < nfj.size());
    Node x = nfi[index];
    Node y = nfj[index];
    Trace("strings-solve-debug")
        << "...Processing(DEQ) " << x << " " << y << std::endl;
    if (d_state.areEqual(x, y))
    {
      // The normal forms have an equivalent term at `index` in the current
      // context. We move to the next `index`.
      //
      // E.g. x ++ x' ++ ... != z ++ y' ++ ... ^ x = z
      index++;
      continue;
    }

    Assert(!x.isConst() || !y.isConst());
    std::vector<Node> lenExp;
    Node xLenTerm = d_state.getLength(x, lenExp);
    Node yLenTerm = d_state.getLength(y, lenExp);
    if (d_state.areDisequal(xLenTerm, yLenTerm))
    {
      // Below, we deal with the cases where the components at the current
      // index are of different lengths in the current context.
      //
      // E.g. x ++ x' ++ ... != y ++ y' ++ ... ^ len(x) != len(y)
      if (x.isConst() || y.isConst())
      {
        Node ck = x.isConst() ? x : y;
        Node nck = x.isConst() ? y : x;
        Node nckLenTerm = x.isConst() ? yLenTerm : xLenTerm;
        Node expNonEmpty = d_state.explainNonEmpty(nck);
        if (expNonEmpty.isNull())
        {
          // Either `x` or `y` is a constant and the other may be equal to the
          // empty string in the current context. We split on whether it
          // actually is empty in that case.
          //
          // E.g. x ++ x' ++ ... != "abc" ++ y' ++ ... ^ len(x) != len(y) --->
          //      x = "" v x != ""
          Node emp = Word::mkEmptyWord(nck.getType());
          d_im.sendSplit(nck, emp, Inference::DEQ_DISL_EMP_SPLIT);
          return;
        }

        Node firstChar = Word::getLength(ck) == 1 ? ck : Word::prefix(ck, 1);
        if (d_state.areEqual(nckLenTerm, d_one))
        {
          if (d_state.areDisequal(firstChar, nck))
          {
            // Either `x` or `y` is a constant and the other is a string of
            // length one. If the non-constant is disequal to the first
            // character of the constant in the current context, we satisfy the
            // disequality and there is nothing else to do.
            //
            // E.g. "d" ++ x' ++ ... != "abc" ++ y' ++ ... ^ len(x) = 1
            return;
          }
          else if (!d_state.areEqual(firstChar, nck))
          {
            // Either `x` or `y` is a constant and the other is a string of
            // length one. If we do not know whether the non-constant is equal
            // or disequal to the first character in the constant, we split on
            // whether it is.
            //
            // E.g. x ++ x' ++ ... != "abc" ++ y' ++ ... ^ len(x) = 1 --->
            //      x = "a" v x != "a"
            if (d_im.sendSplit(firstChar,
                               nck,
                               Inference::DEQ_DISL_FIRST_CHAR_EQ_SPLIT,
                               false))
            {
              return;
            }
          }
        }
        else
        {
          // Either `x` or `y` is a constant and it is not know whether the
          // non-empty non-constant is of length one. We split the non-constant
          // into a string of length one and the remainder and split on whether
          // the first character of the constant and the non-constant are
          // equal.
          //
          // E.g. x ++ x' ++ ... != "abc" ++ y' ++ ... ^ len(x) != "" --->
          //      x = k1 ++ k2 ^ len(k1) = 1 ^ (k1 != "a" v x = "a" ++  k2)
          SkolemCache* skc = d_termReg.getSkolemCache();
          Node sk =
              skc->mkSkolemCached(nck, SkolemCache::SK_ID_DC_SPT, "dc_spt");
          d_termReg.registerTermAtomic(sk, LENGTH_ONE);
          Node skr = skc->mkSkolemCached(
              nck, SkolemCache::SK_ID_DC_SPT_REM, "dc_spt_rem");
          Node eq1 = nck.eqNode(nm->mkNode(kind::STRING_CONCAT, sk, skr));
          eq1 = Rewriter::rewrite(eq1);
          Node eq2 =
              nck.eqNode(nm->mkNode(kind::STRING_CONCAT, firstChar, skr));
          std::vector<Node> antec(nfni.d_exp.begin(), nfni.d_exp.end());
          antec.insert(antec.end(), nfnj.d_exp.begin(), nfnj.d_exp.end());
          antec.push_back(expNonEmpty);
          d_im.sendInference(
              antec,
              nm->mkNode(
                  OR, nm->mkNode(AND, eq1, sk.eqNode(firstChar).negate()), eq2),
              Inference::DEQ_DISL_FIRST_CHAR_STRING_SPLIT,
              true);
          d_im.sendPhaseRequirement(eq1, true);
          return;
        }
      }
      else
      {
        // `x` and `y` have different lengths in the current context and they
        // are both non-constants. We split them into parts that have the same
        // lengths.
        //
        // E.g. x ++ x' ++ ... != y ++ y' ++ ... ^ len(x) != len(y) --->
        //      len(k1) = len(x) ^ len(k2) = len(y) ^
        //        (y = k1 ++ k3 v x = k1 ++ k2)
        Trace("strings-solve") << "Non-Simple Case 1 : add lemma " << std::endl;
        std::vector<Node> antec(nfni.d_exp.begin(), nfni.d_exp.end());
        antec.insert(antec.end(), nfnj.d_exp.begin(), nfnj.d_exp.end());
        std::vector<Node> antecNewLits;

        if (d_state.areDisequal(ni, nj))
        {
          antec.push_back(ni.eqNode(nj).negate());
        }
        else
        {
          antecNewLits.push_back(ni.eqNode(nj).negate());
        }
        antecNewLits.push_back(xLenTerm.eqNode(yLenTerm).negate());

        std::vector<Node> conc;
        SkolemCache* skc = d_termReg.getSkolemCache();
        Node sk1 =
            skc->mkSkolemCached(x, y, SkolemCache::SK_ID_DEQ_X, "x_dsplit");
        Node sk2 =
            skc->mkSkolemCached(x, y, SkolemCache::SK_ID_DEQ_Y, "y_dsplit");
        Node sk3 =
            skc->mkSkolemCached(x, y, SkolemCache::SK_ID_DEQ_Z, "z_dsplit");
        d_termReg.registerTermAtomic(sk3, LENGTH_GEQ_ONE);
        Node sk1Len = utils::mkNLength(sk1);
        conc.push_back(sk1Len.eqNode(xLenTerm));
        Node sk2Len = utils::mkNLength(sk2);
        conc.push_back(sk2Len.eqNode(yLenTerm));
        conc.push_back(nm->mkNode(OR,
                                  y.eqNode(utils::mkNConcat(sk1, sk3)),
                                  x.eqNode(utils::mkNConcat(sk2, sk3))));
        d_im.sendInference(antec,
                           antecNewLits,
                           nm->mkNode(AND, conc),
                           Inference::DEQ_DISL_STRINGS_SPLIT,
                           true);
        return;
      }
    }
    else if (d_state.areEqual(xLenTerm, yLenTerm))
    {
      // `x` and `y` have the same length in the current context. We split on
      // whether they are actually equal.
      //
      // E.g. x ++ x' ++ ... != y ++ y' ++ ... ^ len(x) = len(y) --->
      //      x = y v x != y
      Assert(!d_state.areDisequal(x, y));
      if (d_im.sendSplit(x, y, Inference::DEQ_STRINGS_EQ, false))
      {
        return;
      }
    }
    else
    {
      // It is not known whether `x` and `y` have the same length in the
      // current context. We split on whether they do.
      //
      // E.g. x ++ x' ++ ... != y ++ y' ++ ... --->
      //      len(x) = len(y) v len(x) != len(y)
      if (d_im.sendSplit(xLenTerm, yLenTerm, Inference::DEQ_LENS_EQ))
      {
        return;
      }
    }
  }
  Unreachable();
}

bool CoreSolver::processReverseDeq(std::vector<Node>& nfi,
                                   std::vector<Node>& nfj,
                                   Node ni,
                                   Node nj)
{
  // reverse normal form of i, j
  std::reverse(nfi.begin(), nfi.end());
  std::reverse(nfj.begin(), nfj.end());

  size_t index = 0;
  bool ret = processSimpleDeq(nfi, nfj, ni, nj, index, true);

  // reverse normal form of i, j
  std::reverse(nfi.begin(), nfi.end());
  std::reverse(nfj.begin(), nfj.end());

  return ret;
}

bool CoreSolver::processSimpleDeq(std::vector<Node>& nfi,
                                  std::vector<Node>& nfj,
                                  Node ni,
                                  Node nj,
                                  size_t& index,
                                  bool isRev)
{
  TypeNode stype = ni.getType();
  Node emp = Word::mkEmptyWord(stype);

  NormalForm& nfni = getNormalForm(ni);
  NormalForm& nfnj = getNormalForm(nj);
  while (index < nfi.size() || index < nfj.size())
  {
    if (index >= nfi.size() || index >= nfj.size())
    {
      // We have reached the end of one of the normal forms. Note that this
      // function only deals with two normal forms that have the same length,
      // so the remainder of the longer normal form needs to be empty. This
      // will lead to a conflict.
      //
      // E.g. px ++ x ++ ... != py ^
      //        len(px ++ x ++ ...) = len(py) --->
      //      x = "" ^ ...
      Trace("strings-solve-debug")
          << "Disequality normalize empty" << std::endl;
      std::vector<Node> ant;
      Node niLenTerm = d_state.getLengthExp(ni, ant, nfni.d_base);
      Node njLenTerm = d_state.getLengthExp(nj, ant, nfnj.d_base);
      ant.push_back(niLenTerm.eqNode(njLenTerm));
      ant.insert(ant.end(), nfni.d_exp.begin(), nfni.d_exp.end());
      ant.insert(ant.end(), nfnj.d_exp.begin(), nfnj.d_exp.end());
      std::vector<Node> cc;
      std::vector<Node>& nfk = index >= nfi.size() ? nfj : nfi;
      for (size_t k = index; k < nfk.size(); k++)
      {
        cc.push_back(nfk[k].eqNode(emp));
      }
      Node conc = cc.size() == 1
                      ? cc[0]
                      : NodeManager::currentNM()->mkNode(kind::AND, cc);
      d_im.sendInference(ant, conc, Inference::DEQ_NORM_EMP, true);
      return true;
    }

    // We have inferred that the normal forms up to position `index` are
    // equivalent. Below, we refer to the components at the current position of
    // the normal form as `x` and `y`.
    //
    // E.g. x ++ ... = y ++ ...
    Node x = nfi[index];
    Node y = nfj[index];
    Trace("strings-solve-debug")
        << "...Processing(QED) " << x << " " << y << std::endl;
    if (d_state.areEqual(x, y))
    {
      // The normal forms have an equivalent term at `index` in the current
      // context. We move to the next `index`.
      //
      // E.g. x ++ x' ++ ... != z ++ y' ++ ... ^ x = z
      index++;
      continue;
    }

    if (!x.isConst() || !y.isConst())
    {
      std::vector<Node> lenExp;
      Node xLenTerm = d_state.getLength(x, lenExp);
      Node yLenTerm = d_state.getLength(y, lenExp);
      if (d_state.areEqual(xLenTerm, yLenTerm) && d_state.areDisequal(x, y))
      {
        // Either `x` or `y` is non-constant, the lengths are equal, and `x`
        // and `y` are disequal in the current context. The disequality is
        // satisfied and there is nothing else to do.
        //
        // E.g. x ++ x' ++ ... != y ++ y' ++ ... ^ len(x) = len(y) ^ x != y
        Trace("strings-solve")
            << "Simple Case 2 : found equal length disequal sub strings " << x
            << " " << y << std::endl;
        return true;
      }
      else
      {
        // Either `x` or `y` is non-constant but it is not known whether they
        // have the same length or are disequal. We bail out.
        //
        // E.g. x ++ x' ++ ... != y ++ y' ++ ...
        return false;
      }
    }

    // Below, we deal with the cases where both `x` and `y` are constant.
    Assert(x.isConst() && y.isConst());
    size_t xLen = Word::getLength(x);
    size_t yLen = Word::getLength(y);
    size_t shortLen = std::min(xLen, yLen);
    bool isSameFix =
        isRev ? Word::rstrncmp(x, y, shortLen) : Word::strncmp(x, y, shortLen);
    if (!isSameFix)
    {
      // `x` and `y` are constants but do not share a prefix/suffix, thus
      // satisfying the disequality. There is nothing else to do.
      //
      // E.g. "abc" ++ x' ++ ... != "d" ++ y' ++ ...
      return true;
    }

    // `x` and `y` are constants and share a prefix/suffix. We move the common
    // prefix/suffix to a separate component in the normal form.
    //
    // E.g. "abc" ++ x' ++ ... != "ab" ++ y' ++ ... --->
    //      "ab" ++ "c" ++ x' ++ ... != "ab" ++ y' ++ ...
    Node nk = xLen < yLen ? x : y;
    Node nl = xLen < yLen ? y : x;
    Node remainderStr;
    if (isRev)
    {
      remainderStr = Word::substr(nl, 0, Word::getLength(nl) - shortLen);
      Trace("strings-solve-debug-test")
          << "Rev. Break normal form of " << nl << " into " << nk << ", "
          << remainderStr << std::endl;
    }
    else
    {
      remainderStr = Word::substr(nl, shortLen);
      Trace("strings-solve-debug-test")
          << "Break normal form of " << nl << " into " << nk << ", "
          << remainderStr << std::endl;
    }
    if (xLen < yLen)
    {
      nfj.insert(nfj.begin() + index + 1, remainderStr);
      nfj[index] = nfi[index];
    }
    else
    {
      nfi.insert(nfi.begin() + index + 1, remainderStr);
      nfi[index] = nfj[index];
    }

    index++;
  }
  return false;
}

void CoreSolver::addNormalFormPair( Node n1, Node n2 ){
  if (n1>n2)
  {
    addNormalFormPair(n2,n1);
    return;
  }
  if( !isNormalFormPair( n1, n2 ) ){
    int index = 0;
    NodeIntMap::const_iterator it = d_nfPairs.find(n1);
    if (it != d_nfPairs.end())
    {
      index = (*it).second;
    }
    d_nfPairs[n1] = index + 1;
    if( index<(int)d_nf_pairs_data[n1].size() ){
      d_nf_pairs_data[n1][index] = n2;
    }else{
      d_nf_pairs_data[n1].push_back( n2 );
    }
    Assert(isNormalFormPair(n1, n2));
  } else {
    Trace("strings-nf-debug") << "Already a normal form pair " << n1 << " " << n2 << std::endl;
  }
}

bool CoreSolver::isNormalFormPair( Node n1, Node n2 ) {
  if (n1>n2)
  {
    return isNormalFormPair(n2,n1);
  }
  //Trace("strings-debug") << "is normal form pair. " << n1 << " " << n2 << std::endl;
  NodeIntMap::const_iterator it = d_nfPairs.find(n1);
  if (it != d_nfPairs.end())
  {
    Assert(d_nf_pairs_data.find(n1) != d_nf_pairs_data.end());
    for( int i=0; i<(*it).second; i++ ){
      Assert(i < (int)d_nf_pairs_data[n1].size());
      if( d_nf_pairs_data[n1][i]==n2 ){
        return true;
      }
    }
  }
  return false;
}

void CoreSolver::checkNormalFormsDeq()
{
  eq::EqualityEngine* ee = d_state.getEqualityEngine();
  std::vector< std::vector< Node > > cols;
  std::vector< Node > lts;
  std::map< Node, std::map< Node, bool > > processed;
  
  const context::CDList<Node>& deqs = d_state.getDisequalityList();

  //for each pair of disequal strings, must determine whether their lengths are equal or disequal
  for (const Node& eq : deqs)
  {
    Node n[2];
    for( unsigned i=0; i<2; i++ ){
      n[i] = ee->getRepresentative( eq[i] );
    }
    if( processed[n[0]].find( n[1] )==processed[n[0]].end() ){
      processed[n[0]][n[1]] = true;
      Node lt[2];
      for( unsigned i=0; i<2; i++ ){
        EqcInfo* ei = d_state.getOrMakeEqcInfo(n[i], false);
        lt[i] = ei ? ei->d_lengthTerm : Node::null();
        if( lt[i].isNull() ){
          lt[i] = eq[i];
        }
        lt[i] = NodeManager::currentNM()->mkNode( kind::STRING_LENGTH, lt[i] );
      }
      if (!d_state.areEqual(lt[0], lt[1]) && !d_state.areDisequal(lt[0], lt[1]))
      {
        d_im.sendSplit(lt[0], lt[1], Inference::DEQ_LENGTH_SP);
      }
    }
  }

  if (!d_im.hasProcessed())
  {
    d_state.separateByLength(d_strings_eqc, cols, lts);
    for( unsigned i=0; i<cols.size(); i++ ){
      if (cols[i].size() > 1 && !d_im.hasPendingLemma())
      {
        if (Trace.isOn("strings-solve"))
        {
          Trace("strings-solve") << "- Verify disequalities are processed for "
                                 << cols[i][0] << ", normal form : ";
          utils::printConcatTrace(getNormalForm(cols[i][0]).d_nf, "strings-solve");
          Trace("strings-solve")
              << "... #eql = " << cols[i].size() << std::endl;
        }
        //must ensure that normal forms are disequal
        for( unsigned j=0; j<cols[i].size(); j++ ){
          for( unsigned k=(j+1); k<cols[i].size(); k++ ){
            //for strings that are disequal, but have the same length
            if (cols[i][j].isConst() && cols[i][k].isConst())
            {
              // if both are constants, they should be distinct, and its trivial
              Assert(cols[i][j] != cols[i][k]);
            }
            else
            {
              if (d_state.areDisequal(cols[i][j], cols[i][k]))
              {
                Assert(!d_state.isInConflict());
                if (Trace.isOn("strings-solve"))
                {
                  Trace("strings-solve") << "- Compare " << cols[i][j] << " ";
                  utils::printConcatTrace(getNormalForm(cols[i][j]).d_nf, "strings-solve");
                  Trace("strings-solve") << " against " << cols[i][k] << " ";
                  utils::printConcatTrace(getNormalForm(cols[i][k]).d_nf, "strings-solve");
                  Trace("strings-solve") << "..." << std::endl;
                }
                processDeq(cols[i][j], cols[i][k]);
                if (d_im.hasProcessed())
                {
                  return;
                }
              }
            }
          }
        }
      }
    }
  }
}

void CoreSolver::checkLengthsEqc() {
  for (unsigned i = 0; i < d_strings_eqc.size(); i++)
  {
    TypeNode stype = d_strings_eqc[i].getType();
    NormalForm& nfi = getNormalForm(d_strings_eqc[i]);
    Trace("strings-process-debug")
        << "Process length constraints for " << d_strings_eqc[i] << std::endl;
    // check if there is a length term for this equivalence class
    EqcInfo* ei = d_state.getOrMakeEqcInfo(d_strings_eqc[i], false);
    Node lt = ei ? ei->d_lengthTerm : Node::null();
    if (lt.isNull())
    {
      Trace("strings-process-debug")
          << "No length term for eqc " << d_strings_eqc[i] << std::endl;
      continue;
    }
    Node llt = NodeManager::currentNM()->mkNode(kind::STRING_LENGTH, lt);
    // now, check if length normalization has occurred
    if (ei->d_normalizedLength.get().isNull())
    {
      Node nf = utils::mkNConcat(nfi.d_nf, stype);
      if (Trace.isOn("strings-process-debug"))
      {
        Trace("strings-process-debug")
            << "  normal form is " << nf << " from base " << nfi.d_base
            << std::endl;
        Trace("strings-process-debug") << "  normal form exp is: " << std::endl;
        for (const Node& exp : nfi.d_exp)
        {
          Trace("strings-process-debug") << "   " << exp << std::endl;
        }
      }

      // if not, add the lemma
      std::vector<Node> ant;
      ant.insert(ant.end(), nfi.d_exp.begin(), nfi.d_exp.end());
      ant.push_back(nfi.d_base.eqNode(lt));
      Node lc = NodeManager::currentNM()->mkNode(kind::STRING_LENGTH, nf);
      Node lcr = Rewriter::rewrite(lc);
      Trace("strings-process-debug")
          << "Rewrote length " << lc << " to " << lcr << std::endl;
      if (!d_state.areEqual(llt, lcr))
      {
        Node eq = llt.eqNode(lcr);
        ei->d_normalizedLength.set(eq);
        d_im.sendInference(ant, eq, Inference::LEN_NORM, true);
      }
    }
  }
}

bool CoreSolver::processInferInfo(CoreInferInfo& cii)
{
  InferInfo& ii = cii.d_infer;
  // rewrite the conclusion, ensure non-trivial
  ii.d_conc = Rewriter::rewrite(ii.d_conc);

  if (ii.isTrivial())
  {
    // conclusion rewrote to true
    return false;
  }
  // process the state change to this solver
  if (!cii.d_nfPair[0].isNull())
  {
    Assert(!cii.d_nfPair[1].isNull());
    addNormalFormPair(cii.d_nfPair[0], cii.d_nfPair[1]);
  }
  // send phase requirements
  for (const std::pair<const Node, bool> pp : cii.d_pendingPhase)
  {
    d_im.sendPhaseRequirement(pp.first, pp.second);
  }

  // send the inference, which is a lemma
  d_im.sendInference(ii, true);

  return true;
}

}/* CVC4::theory::strings namespace */
}/* CVC4::theory namespace */
}/* CVC4 namespace */
