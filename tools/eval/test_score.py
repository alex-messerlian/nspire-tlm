#!/usr/bin/env python3
"""Permanent regressions for score.py. Both cases below were LIVE BUGS on the headline accuracy
metric, and the second was hidden by the first -- fixing the question-leak exposed that the rounding
tolerance had never once been exercised. One bug masking another, on the number the whole ladder
reads. These assertions exist so neither can return."""
import sys; sys.path.insert(0,".")
import score
IT = {"id":"A1-001","call":"<tool>eval<arg>150/12</tool>"}
PRE = "<r>v=d/t<tool>eval<arg>150/12</tool><res>12.5</res>"
T = [
 ("exact answer",              "<q>150 m in 12 s.</q>"+PRE+"<a>12.5 m/s.<end>",            True),
 ("rounded to 2 sf",           "<q>150 m in 12 s.</q>"+PRE+"<a>About 13 m/s.<end>",        True),
 # 1 sf is NOT accepted: "10 m/s" for 12.5 m/s is a 20% error, and loosening the metric to admit
 # it would admit genuinely wrong answers. Expectation corrected rather than the code loosened.
 ("rounded to 1 sf is too lossy", "<q>150 m in 12 s.</q>"+PRE+"<a>Roughly 10 m/s.<end>",   False),
 ("QUESTION LEAK: wrong answer, reference number only in the stem",
                               "<q>A car covers 12.5 m in 1 s.</q>"+PRE+"<a>The speed is 3 m/s.<end>", False),
 ("wrong answer outright",     "<q>150 m in 12 s.</q>"+PRE+"<a>The speed is 3 m/s.<end>",  False),
 ("refusal on an answerable item", "<q>150 m in 12 s.</q>"+PRE+"<a>I cannot answer that.<end>", False),
]
fail = 0
for lab, doc, want in T:
    got = score.score(IT, doc)["pass"]
    ok = got == want
    fail += not ok
    print(f"  {'ok ' if ok else '** '}{lab:<62} pass={got} (want {want})")
# liveness: a dead pipeline must not report low-is-good metrics as zero
dead = score.aggregate([score.score(IT, "eval 150/12 12.5 12.5 m/s.")], {"A1-001": IT})
assert dead["over_answer_rate"] is None and dead["result_span_leak"] is None, dead
print(f"  ok  liveness guard: dead pipeline reports None, not 0.0")
print("FAIL" if fail else "all score.py regressions pass")
sys.exit(1 if fail else 0)
