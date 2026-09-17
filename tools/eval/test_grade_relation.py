#!/usr/bin/env python3
"""grade.relation_stated must accept a readable spelling and reject a wrong relation.

WHY IT EXISTS. explain_ok compared the record's formula to the answer with whitespace removed and
nothing else. The store spells `P=(((V)^(2))/(R))` and a written explanation says `P = V^2/R`, so
265 of 492 written variants -- 53.9% -- scored FALSE for stating the same relation readably. The
arm would have reported roughly half its true value and it would have read as the model failing.

The loosening is a normalisation and it has a stated cost: dropping all parentheses also equates
`(a+b)*c` with `a+b*c`. What must NOT be lost is sign and symbol sensitivity, because every wrong
relation observed on a real checkpoint was one of those. Both directions are asserted here.
"""
import pathlib, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import grade

ACCEPT = [
    ("P=(((V)^(2))/(R))", "At a wall outlet the voltage is fixed: P = V^2/R. Lower R, more power."),
    ("I_0=((V_0)/(Z))", "with impedance Z standing in for resistance: I_0 = V_0/Z"),
    ("T=2*pi*sqrt(((m)/(k)))", "That is T = 2*pi*sqrt(m/k). Mass sits on top"),
    ("F=-k*x", "Stretch a spring and it pulls back: F = -k*x. The minus sign means"),
    ("F=m*a", "F = m*a is a trade."),
    ("W=F*d", "That gives 30 J. Directly from W=F*d."),
    ("a=(vf-vi)/t", "a = (vf-vi)/t spreads the whole change in velocity evenly"),
]
# Every one of these was produced by a real checkpoint and is the failure the arm must catch.
REJECT = [
    ("F=-k*x", "F = k*x is how long that force is"),            # the sign is gone
    ("F=-k*x", "because F = G*x is not a big Delta_t"),          # G is invented
    ("F=m*a", "the record says nothing about a relation here"),  # no relation at all
    ("E=K+U", "E = K - U only holds while nothing rubs"),        # operator flipped
    ("W=F*d", "W = F*t is the relation"),                        # wrong symbol
]

F = 0
for f, t in ACCEPT:
    got = grade.relation_stated(f, t)
    if not got:
        F += 1
    print(f"  {'PASS' if got else 'FAIL'}  accept  {f:24s} <- {t[:46]!r}")
for f, t in REJECT:
    got = grade.relation_stated(f, t)
    if got:
        F += 1
    print(f"  {'PASS' if not got else 'FAIL'}  reject  {f:24s} <- {t[:46]!r}")

# The corpus it grades: a predicate that rejects most of its own subject is the wrong predicate.
import json
E = json.load(open(pathlib.Path(__file__).resolve().parents[2] / "corpus/knowledge/explanations.json"))
ok = sum(1 for r in E for v in r["variants"] if grade.relation_stated(r["formula"], v))
tot = sum(len(r["variants"]) for r in E)
good = ok > 0.9 * tot
F += 0 if good else 1
print(f"  {'PASS' if good else 'FAIL'}  {ok}/{tot} written variants ({100*ok/tot:.1f}%) are seen to "
      f"state their relation")

print(f"\n{'FAIL' if F else 'PASS'} test_grade_relation: {F} failure{'' if F == 1 else 's'}")
sys.exit(1 if F else 0)
