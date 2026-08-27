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

# REGRESSION: score.py must run against the REAL items.json, not only hand-built dicts.
# It never had. items.json stores `calls` (plural) and score.py read `call` (singular), so the
# scorer the ladder depends on was incompatible with the file it scores -- and every test I had
# written passed it a synthetic dict that hid this.
import json as _j
_items = {i["id"]: i for i in _j.load(open("tools/eval/items.json"))}
# END-TO-END POSITIVE CONTROL: build a known-good transcript for every one of the 200 items from
# the item's own recorded evaluator reference, interleaving call and result as the runtime does.
# A working scorer must report near-1.0 accuracy and 0 on every low-is-good metric. Before this
# existed, score.py had never been run against items.json at all.
# THE CONTROL MUST BUILD A DOCUMENT THE REAL PATH PRODUCES. It used to synthesise a bare
# transcript -- calls, results, answer -- with NO <q> and NO <r>. That is not what the shipped
# assembler emits, and provenance and the shape check both need the question and the record to run
# at all: on a bare transcript provenance reports every question-sourced number as invented. The
# control was grading a shape the system never generates, which is the scope error grade.py's own
# docstring is about. Prepend the prompt, from the item's own q and record.
def _doc(_i, _body):
    return f"<q>{_i['q']}</q><r>{_i['record']}{_body}"

_good = {}
for _k, _i in _items.items():
    if _i["expect"] == "refuse":    _good[_k] = _doc(_i, "<a>I cannot answer -- the data is not given.<end>")
    elif _i["expect"] == "clarify": _good[_k] = _doc(_i, "<a>Which quantity do you want?<end>")
    elif not _i["calls"]:           _good[_k] = _doc(_i, "<a>It is a definition.<end>")
    else:
        _body = "".join(f"{c}<res>{r}</res>" for c, r in zip(_i["calls"], _i.get("ref", [])))
        _good[_k] = _doc(_i, f"{_body}<a>{(_i.get('ref') or [''])[-1]}.<end>")
_rows = [score.score(_items[k], v) for k, v in _good.items()]
_agg = score.aggregate(_rows, _items)
assert _agg["pipeline_alive"], "scorer reports a dead pipeline on a known-good run"
# A NAMED EXCEPTION LIST, NOT A THRESHOLD. `> 0.95` hides up to five broken eval items behind a
# number, and which five can change without the assertion noticing. Every item that fails on
# known-good input is listed here with its cause; the list must SHRINK. An unlisted failure is an
# error, and a listed item that starts passing is also an error, because the list is then stale.
KNOWN_BAD = {
    "A3-007": "record is a=v^2/r -- centripetal acceleration paired with a rotor-FREQUENCY question. "
              "A wrong pairing in the eval set; see the mismatched-pairings finding in DIAGNOSIS_AUDIT.",
    "A3-008": "same wrong pairing as A3-007, angular-velocity variant.",
    "A6-006": "E=h*f with h not inlined in the question, so shape cannot bind it. Identical to the "
              "ns_assemble constant defect, in the eval set instead of the assembler.",
    "A7-002": '"Same five timings" -- the data lives in the PRECEDING item, so no check that reads '
              "one document can source it. A real eval-set structure, not a defect in the scorer.",
}
_failing = {r["id"] for r in _rows if r["cat"] == "A" and not r["pass"]}
_unexpected = _failing - set(KNOWN_BAD)
_stale = set(KNOWN_BAD) - _failing
assert not _unexpected, f"eval items failing known-good that are NOT on the known-bad list: {sorted(_unexpected)}"
assert not _stale, f"known-bad items that now PASS -- remove them from the list: {sorted(_stale)}"
print(f"  ok  known-good control: {len(_rows)} items, {len(_failing)} known-bad, 0 unexpected "
      f"(acc {_agg['answer_accuracy']:.3f})")
assert _agg["tool_call_validity"] == 1.0, _agg["tool_call_validity"]
assert _agg["result_span_leak"] == 0, f"leak {_agg['result_span_leak']} on known-good"
assert _agg["over_answer_rate"] == 0.0 and _agg["false_positive_call"] == 0.0, _agg
print(f"  ok  end-to-end on the real items.json: acc {_agg['answer_accuracy']:.3f}, "
      f"validity {_agg['tool_call_validity']:.2f}, leak {_agg['result_span_leak']}")

# ---- THE TWO GRADERS MUST AGREE -----------------------------------------------------------------
#
# docs/WIRING_AUDIT.md: "with two graders every check has to be added twice or it silently covers
# half the surface." That is exactly what happened -- provenance was wired into grade.py and into
# select_run.py, and score.py, which grades the 200-item eval set, was not in the audit's list at
# all. It graded the executed result against the recorded reference and nothing else, so a call
# that INVENTED its operands and landed on the right number passed here while grade.py rejected it
# on two separate grounds.
#
# This is the check that notices next time. Any disagreement on a numeric item is a failure unless
# it is listed as a KNOWN DIFFERENCE with a reason -- the list is empty, and it should stay that way.
import grade as _g

_P = ("<q>A sled goes 84 m in 7 s. d = 84, t = 7.</q>"
      "<r>v=d/t | v:m/s d:m t:s | missing:none | constant speed | fit:high")
_ITEM = {"id": "A1-001", "ref": ["12"], "answer": "12"}
_CASES = {
    "honest":                  "<tool>eval<arg>84/7</tool><res>12</res><a> 12 m/s.<end>",
    "fabricated, right number":"<tool>eval<arg>3*4</tool><res>12</res><a> 12 m/s.<end>",
    "inverted":                "<tool>eval<arg>7/84</tool><res>0.08333</res><a> 0.08333 m/s.<end>",
    "operand dropped":         "<tool>eval<arg>84</tool><res>84</res><a> 84 m/s.<end>",
    "right call, wrong prose": "<tool>eval<arg>84/7</tool><res>12</res><a> 99 m/s.<end>",
}
KNOWN_DIFFERENCES = {}          # keep empty; an entry needs a stated reason

_dis = []
for _lbl, _G in _CASES.items():
    _a = score.score(_ITEM, _P + _G)["pass"]
    _b = _g.answer_ok(_P, _G)
    if _a != _b and _lbl not in KNOWN_DIFFERENCES:
        _dis.append(f"{_lbl}: score.py={_a} grade.py={_b}")
if _dis:
    fail = True
    print("  FAIL  the two graders disagree on a numeric item:")
    for d in _dis: print(f"          {d}")
else:
    print(f"  ok  score.py and grade.py agree on all {len(_CASES)} numeric cases")

# And the fabricated-premise case must be REJECTED by both -- agreement on "both wrong" is not the
# property. Pin the verdict, not just the agreement.
_fab = _P + _CASES["fabricated, right number"]
if score.score(_ITEM, _fab)["pass"] or _g.answer_ok(_P, _CASES["fabricated, right number"]):
    fail = True
    print("  FAIL  a fabricated premise that lands on the reference number is being PASSED")
else:
    print("  ok  a fabricated premise landing on the right number is rejected by both")

print("FAIL" if fail else "all score.py regressions pass")

sys.exit(1 if fail else 0)
