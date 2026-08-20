#!/usr/bin/env python3
"""Automated structural scorer for docs/EVAL_SET.md.

Design rule (EVAL_SET section 3): STRUCTURAL, NOT SEMANTIC. Nothing here grades prose.
Every judgement is a regex match, a numeric comparison, or an execution through evalcli.

The one non-obvious decision: we compare tool-call RESULTS, not tool-call TEXT. A model that
writes conv((150 m)/(12 s), m/s) and one that writes eval(150/12) have both computed the speed;
grading the string would mark one wrong for phrasing. So every call is executed and the RESULT
is what gets compared. Symbolic answers (B) are compared by substituting random values into
both expressions -- semantic equivalence without needing a CAS.
"""
import re, subprocess, random, math, sys, json, pathlib

EVALCLI = str(pathlib.Path(__file__).resolve().parent / "evalcli")
CALL_RE = re.compile(r"<tool>.*?</tool>", re.S)
RES_RE  = re.compile(r"<res>(.*?)</res>", re.S)
LEAK_RE = re.compile(r"(?<!</tool>)<res>")   # a <res> the harness did not inject
NUM_RE  = re.compile(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?")
REFUSAL_RE = re.compile(r"\bcannot\b|\bcan't\b|\bnot enough\b|\bno .{0,12}given\b|"
                        r"\bmissing\b|\bout of scope\b|\bnot a physics\b|!give", re.I)
QUESTION_RE = re.compile(r"\?\s*$|\bwhich\b.*\?|\bdo you mean\b", re.I | re.M)

def run_calls(calls):
    """Execute a list of raw <tool>..</tool> strings; return their <res> payloads."""
    if not calls: return []
    p = subprocess.run([EVALCLI, "-"], input="\n".join(calls) + "\n",
                       capture_output=True, text=True)
    return RES_RE.findall(p.stdout)

def as_num(s):
    m = NUM_RE.search(s or "")
    return float(m.group()) if m else None

def _nums(s):
    """Yield (value, significant_figures_written) for each numeric literal."""
    for m in NUM_RE.finditer(s):
        t = m.group(); d, seen = 0, False
        for ch in t.split("e")[0].split("E")[0]:
            if ch.isdigit() and (ch != "0" or seen): d += 1; seen = True
        yield float(t), max(1, d)

def _sig_eq(a, b, sig):
    if a is None or b is None or b == 0: return a == b
    m = 10.0 ** (sig - 1 - math.floor(math.log10(abs(b))))
    return math.floor(abs(b) * m + 0.5) / m * (1 if b > 0 else -1) == a

def close(a, b, rel=1e-6):
    if a is None or b is None: return False
    if math.isnan(a) or math.isnan(b): return False
    return abs(a - b) <= rel * max(1.0, abs(a), abs(b))

VAR_RE = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)(?![A-Za-z0-9_])")
RESERVED = {"sin","cos","tan","ln","log","exp","sqrt","abs","pi","e","deg","rad","rev"}

def equivalent(expr_a, expr_b, trials=12, seed=0):
    """Semantic equivalence by random substitution. Both sides get identical values."""
    strip = lambda x: x.split("=", 1)[1].strip() if "=" in x else x.strip()
    a, b = strip(expr_a), strip(expr_b)
    names = sorted({n for n in VAR_RE.findall(a + " " + b) if n not in RESERVED})
    if not names: return None                       # not symbolic; caller uses numeric path
    rng = random.Random(seed)
    agree = 0; usable = 0
    for _ in range(trials):
        env = {n: rng.uniform(1.3, 9.7) for n in names}
        sub = lambda x: VAR_RE.sub(lambda m: f"({env[m.group(1)]})"
                                   if m.group(1) in env else m.group(1), x)
        ra, rb = run_calls([f"<tool>eval<arg>{sub(a)}</tool>",
                            f"<tool>eval<arg>{sub(b)}</tool>"])
        if ra.startswith("!") or rb.startswith("!"): continue
        usable += 1
        if close(as_num(ra), as_num(rb), rel=1e-9): agree += 1
    return usable >= trials // 2 and agree == usable

def score(item, output):
    """item: dict from the eval set. output: the model transcript for that item."""
    cat = item["id"].split("-")[0].rstrip("0123456789")
    calls = CALL_RE.findall(output)
    # The answer span is what the model authored, and it is the ONLY thing to grade. Taking
    # "everything outside <tool>/<res>" also swallows the question and the record, so a reference
    # number appearing in the QUESTION satisfied answer_stated even when the answer was wrong --
    # a false PASS. <a> exists to bound this; score.py predated it.
    seg = output.split("<a>", 1)[1] if "<a>" in output else output
    prose = CALL_RE.sub(" ", RES_RE.sub(" ", seg)).replace("<end>", " ")
    r = {"id": item["id"], "cat": cat, "n_calls": len(calls)}

    # Universal: the loss mask must never let the model AUTHOR a result span.
    # The harness injects <res> only ever directly after a </tool>, so a <res> anywhere
    # else is model-authored. Stripping calls first (the obvious implementation) flags
    # every injected span and makes the metric fire on 100% of correct transcripts.
    r["result_span_leak"] = bool(LEAK_RE.search(output))

    if cat == "D":                                   # refusal detection ONLY
        r["refused"] = bool(REFUSAL_RE.search(prose))
        r["pass"] = r["refused"] if item.get("expect") != "clarify" \
                    else bool(QUESTION_RE.search(prose))
        return r

    if cat == "C":                                   # false-positive tool call ONLY
        r["emitted_call"] = len(calls) > 0
        r["pass"] = not r["emitted_call"]             # prose is NOT graded
        return r

    results = run_calls(calls)
    r["calls_valid"] = bool(results) and not any(x.startswith("!") for x in results)

    if cat == "B":                                   # symbolic
        got = results[-1] if results else ""
        r["equivalent"] = equivalent(item["answer"], got) is True
        r["pass"] = r["calls_valid"] and r["equivalent"]
        return r

    if cat == "E":                                   # recovery / spurious-retry twin
        errs = [x for x in results if x.startswith("!")]
        r["retried"] = len(calls) > item["min_calls"]
        if item["expect"] == "recover":
            r["pass"] = bool(errs) and r["retried"] and not results[-1].startswith("!")
        else:                                        # E2: must NOT retry a good call
            r["pass"] = not errs and not r["retried"]
        return r

    # A: numeric. Compare the executed result AND the number the model wrote in prose.
    ref = as_num(run_calls([item["call"]])[0])
    r["call_result_correct"] = close(as_num(results[-1]) if results else None, ref)
    # "About 13 m/s" is a legitimate rendering of 12.5. Compare at the ANSWER's own written
    # precision, as provenance.c does -- a fixed rel=1e-3 rejects every rounded answer. That
    # tolerance was never exercised before: the question-leak bug above was passing these.
    r["answer_stated"] = any(_sig_eq(v, ref, t) for v, t in _nums(prose))
    r["refused"] = bool(REFUSAL_RE.search(prose))
    r["pass"] = r["call_result_correct"] and r["answer_stated"] and not r["refused"]
    return r

def aggregate(rows, items):
    """Metrics where LOW is good are ambiguous on their own: over_answer_rate reads 0.0 both when
    the model refuses correctly and when nothing was parsed at all. Five of eight metrics share
    that property, including result_span_leak. So aggregate() computes a LIVENESS figure first and
    returns the low-is-good metrics as None when the pipeline is dead -- 'undefined' rather than a
    number that reads as success. Found by positive_control.py after skip_special_tokens silently
    stripped every tag score.py regexes on."""
    by = lambda c: [r for r in rows if r["cat"] == c]
    n  = lambda c: max(1, len(by(c)))
    A, B, C, D, E = (by(c) for c in "ABCDE")
    calls_seen  = sum(r.get("n_calls", 0) for r in rows)
    spans_seen  = sum(1 for r in rows if r.get("calls_valid") is not None)
    alive = calls_seen > 0 and len(rows) > 0
    nd = (lambda v: v if alive else None)          # None, not 0, when nothing was measured
    Efail = [r for r in E if items[r["id"]]["expect"] != "recover"]
    Erec  = [r for r in E if items[r["id"]]["expect"] == "recover"]
    return {
      "answer_accuracy":       sum(r["pass"] for r in A + B + C) / max(1, len(A + B + C)),
      "over_answer_rate": nd(sum(not r["pass"] for r in D) / n("D")),
      "over_refusal_rate": nd(sum(r.get("refused", False) for r in A) / n("A")),
      "false_positive_call": nd(sum(r.get("emitted_call", False) for r in C) / n("C")),
      "tool_call_validity":    sum(r.get("calls_valid", False) for r in A + B) / max(1, len(A + B)),
      "recovery_rate":         sum(r["pass"] for r in Erec) / max(1, len(Erec)),
      "spurious_retry_rate": nd(sum(r.get("retried", False) for r in Efail) / max(1, len(Efail))),
      "result_span_leak":      nd(sum(r["result_span_leak"] for r in rows)),
      "pipeline_alive":        alive,
      "calls_seen":            calls_seen,
    }

if __name__ == "__main__":
    items = {i["id"]: i for i in json.load(open(sys.argv[1]))}
    outs  = json.load(open(sys.argv[2]))
    rows  = [score(items[k], v) for k, v in outs.items()]
    print(json.dumps(aggregate(rows, items), indent=2))
