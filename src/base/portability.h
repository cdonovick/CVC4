/*********************                                                        */
/*! \file portability.h
 ** \verbatim
 ** Top contributors (to current version):
 **   Caleb Donovick
 ** This file is part of the CVC4 project.
 ** Copyright (c) 2009-2016 by the authors listed in the file AUTHORS
 ** in the top-level source directory) and their institutional affiliations.
 ** All rights reserved.  See the file COPYING in the top-level source
 ** directory for licensing information.\endverbatim
 **
 ** \brief Transparent macros for compiler specific and c++11 features.
 **
 ** Transparent macros for compiler specific and c++11 features.
 **/

#ifndef __CVC4__PORTABILITY_H
#define __CVC4__PORTABILITY_H

/* Portable macros */
#ifndef __CVC4__expect

  #if defined __GUNC__
    /* !cond !expect ensure currect types are passed to __builtin_expect */
    #define __CVC4__expect(cond, expect)  __builtin_expect((!(cond)),(!(expect)))
  #else
    #define __CVC4__expect(cond, expect) (cond)
  #endif

#else /* __CVC4__expect */
  #error "__CVC4__expect was defined outside of " __FILE__ ", something is going to break"
#endif /* __CVC4__expect */

#ifndef __CVC4__FUNC_STRING

  #if defined __GNUC__
    #define __CVC4__FUNC_STRING __PRETTY_FUNCTION__
  #elif  __cplusplus >= 201103l
    #define __CVC4__FUNC_STRING __func__
  #elif defined __FUNCTION__
    #define __CVC4__FUNC_STRING __FUNCTION__
  #else
    // No way to determine the name of the function
    #define __CVC4__FUNC_STRING ""
  #endif

#else /* __CVC4__FUNC_STRING */
  #error "__CVC4__FUNC_STRING was defined outside of " __FILE__ ", something is going to break"
#endif /* __CVC4__FUNC_STRING */

#ifndef __CVC4__noreturn

  #if __cplusplus >= 201103l
    #define __CVC4__noreturn [[noreturn]]
  #elif defined __GNUC__
    #define __CVC4__noreturn __attribute__((noreturn))
  #else
    #define __CVC4__noreturn
  #endif

#else /* __CVC4__NO_RETURN */
  #error "__CVC4__NO_RETURN was defined outside of " __FILE__ ", something is going to break"
#endif /* __CVC4__NO_RETURN */

>>>>>>> a42b0b2567cf7da5d065eecf4270da97d51e5b1f
#endif /* __CVC4__PORTABILITY_H */

