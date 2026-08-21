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
_good = {}
for _k, _i in _items.items():
    if _i["expect"] == "refuse":    _good[_k] = "<a>I cannot answer -- the data is not given.<end>"
    elif _i["expect"] == "clarify": _good[_k] = "<a>Which quantity do you want?<end>"
    elif not _i["calls"]:           _good[_k] = "<a>It is a definition.<end>"
    else:
        _body = "".join(f"{c}<res>{r}</res>" for c, r in zip(_i["calls"], _i.get("ref", [])))
        _good[_k] = f"{_body}<a>{(_i.get('ref') or [''])[-1]}.<end>"
_rows = [score.score(_items[k], v) for k, v in _good.items()]
_agg = score.aggregate(_rows, _items)
assert _agg["pipeline_alive"], "scorer reports a dead pipeline on a known-good run"
assert _agg["answer_accuracy"] > 0.95, f"accuracy {_agg['answer_accuracy']} on known-good"
assert _agg["tool_call_validity"] == 1.0, _agg["tool_call_validity"]
assert _agg["result_span_leak"] == 0, f"leak {_agg['result_span_leak']} on known-good"
assert _agg["over_answer_rate"] == 0.0 and _agg["false_positive_call"] == 0.0, _agg
print(f"  ok  end-to-end on the real items.json: acc {_agg['answer_accuracy']:.3f}, "
      f"validity {_agg['tool_call_validity']:.2f}, leak {_agg['result_span_leak']}")
