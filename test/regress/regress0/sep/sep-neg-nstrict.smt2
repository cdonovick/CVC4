(set-logic ALL_SUPPORTED)
(set-info :status unsat)

(declare-const x Int)
(declare-const y Int)
(declare-const z Int)

(declare-const a Int)
(declare-const b Int)

(assert (not (sep true (pto x a))))
(assert (sep (pto x a) (pto z b)))


(check-sat)