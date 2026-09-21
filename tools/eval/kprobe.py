#!/usr/bin/env python3
"""The knowledge-tier probes from docs/PREREG_KNOWLEDGE.md. Deterministic, per item.

WHY A PROBE AND NOT AN ARM. Arm rates on this hardware carry a 30-point training-seed range (67.8 /
47.5 / 37.8 on three identical configs). A deterministic per-item probe has a 2.5-point range
against that 30. Nothing under ~20 points is decidable from an arm with one seed, so every gate
here is a probe: GREEDY argmax, matching device_app.c, one attempt per item, no best-of. The
per-item outcome is a function of (checkpoint, item) alone and decode variance is exactly zero.

    D1  the device transcript        six questions, the thing the user reported broken
    D4  K1 hit, 150 TRAINED terms    explain the term the record defines
    D5  K3 refuse, the same 150      a compute request against a definition
    D6  K1 on 150 HELD-OUT terms     reported, stratified, NEVER gated

D4 AND D5 ARE ONE MEASUREMENT. A model that explains everything scores 100 and 0; one that refuses
everything scores the reverse. Neither prints without the other.

ITEM SETS ARE HASHED, NOT SAMPLED. `Random(sha256("kprobe/v1:" + term))` picks each item's question
from the shipped frame bank. There is no seed argument and no re-draw: a run that produces a
different item set is a bug, not a new measurement.

usage: kprobe.py <checkpoint.pt> [TOK=train/tok4096.json]
"""
import hashlib, json, os, pathlib, random, re, subprocess, sys

import torch
from tokenizers import Tokenizer

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "vendor/llama2.c"))
sys.path.insert(0, str(ROOT / "tools/eval"))
sys.path.insert(0, str(ROOT / "corpus"))
import genloop                        # THE generation loop; never reimplement it
import knowledge_docs as KD
import asks_explain as AE
from model import Transformer, ModelArgs

TK = Tokenizer.from_file(os.environ.get("TOK", str(ROOT / "train/tok4096.json")))
RES, ENDT, TOOLC = (TK.token_to_id(t) for t in ("<res>", "<end>", "</tool>"))

# The six from the device transcript. Fixed here, not sampled.
#
# FIVE ARE EXPLAIN QUESTIONS AND ONE IS A COMPUTE QUESTION, and the first version of this graded
# all six by the explain standard. "find work when f=12 d=2.5" supplies values and asks for a
# number; the correct answer IS "W = 30 J. W=F*d gives it.", and is_template_only flags it because
# a computed answer looks exactly like the template it is meant to catch.
#
# The device test said of that exact question: "it gave 30J kinda decent". The user was satisfied
# with it. The criterion applied an EXPLAIN standard to a COMPUTE question.
#
# CORRECTING THIS AFTER SEEING A RESULT IS ONLY LEGITIMATE BECAUSE IT CHANGES NOTHING. It mis-graded
# the same item in BOTH runs in the same direction -- know1 1/6 -> 2/6, know2 3/6 -> 4/6 -- and both
# remain below the >= 5 threshold. A correction that flipped a verdict would be fitting the
# criterion to the result, which is what a pre-registration exists to stop. Both numbers are
# reported so the change is visible rather than quiet.
TRANSCRIPT = [("what is hookes law", "explain"), ("newton second law", "explain"),
              ("explain hokes law", "explain"), ("what is kinetic energy", "explain"),
              ("find work when f=12 d=2.5", "compute"), ("what is acceleration", "explain")]


def _run_tool(call):
    p = subprocess.run([str(ROOT / "tools/eval/evalcli"), call], capture_output=True, text=True)
    m = re.search(r"<res>(.*?)</res>", p.stdout, re.S)
    return (m.group(1) if m else "!give"), 0.0


def gen(model, prompt, maxlen=140):
    """GREEDY. sample=None is argmax, which is what device_app.c:623 does."""
    def step(ids):
        lg = model(torch.tensor([ids[-256:]]))[:, -1, :]
        lg[0, RES] = -1e30
        return lg[0]
    return genloop.generate_text(step, lambda t: TK.encode(t).ids,
                                 lambda i: TK.decode(i, skip_special_tokens=False),
                                 TK.encode(prompt).ids,
                                 res_id=RES, end_id=ENDT, toolc_id=TOOLC, run_tool=_run_tool,
                                 max_tokens=maxlen, sample=None)


def _squash(s):
    """Whitespace removed, because the TOKENIZER DECODES TAGS WITH SPACES.

    THIS FUNCTION EXISTS BECAUSE ITS ABSENCE PRODUCED A PUBLISHED 0.0%. D7 and D8 reported
    `called 0/12` and `right target 0/12` for every run, which read as a model that had learned
    nothing from 19,728 R1 and C1 documents. The model was emitting

        <tool> solve<arg> F=m*a<arg> m</tool><res> m=F/a</res><a> m=F/a -- same relation ...

    which is CORRECT, and `"<tool>solve<arg>" in out` is False against it. The true figures on the
    same checkpoint are called 6/12 = 50.0% and right target 6/12 = 50.0%.

    The tell was in this file the whole time: the `stated result` arm three lines below each check
    already compared with `.replace(" ", "")` and scored 50-58%, so one comparison in the function
    was whitespace-insensitive and its neighbours were not. A rate of exactly 0% beside a rate of
    50% on the same generations is the signature of an instrument, not an inability.
    """
    return re.sub(r"\s+", "", s)


# A121. N=12 WAS TOO SMALL AND IT MISLED ME THREE TIMES IN ONE SESSION.
#
# D7 measured 12/12 on two seeds of one corpus and read as 100%. At n=60 the same arm spans
# 75%-100% ACROSS SEEDS on a single corpus -- so the two-seed agreement was a coincidence of a
# sample too small to show the spread, and I used it to conclude (a) that F1 caused a regression,
# (b) that the corpus values caused it, before the discriminating run showed it was SEED variance
# all along. D8 is the same: 12/12 against n=50 readings of 100% and 76%.
#
# n=40 costs about 90 seconds more per probe and resolves a 20 pp difference; 12 cannot resolve 40.
D7_N = int(os.environ.get("D7_N", "40"))


def rng_for(term):
    h = hashlib.sha256(("kprobe/v1:" + term).encode()).hexdigest()
    return random.Random(int(h[:16], 16))


def answer_of(gen_text):
    return gen_text.split("<a>", 1)[1].rsplit("<end>", 1)[0] if "<a>" in gen_text else ""


DECLINE = re.compile(r"\bcannot answer\b", re.I)

# THE TEMPLATE THE RETRAIN EXISTS TO REPLACE. Measured on the SHIPPING checkpoint before this run:
# it scores 5 of 6 on D1 while answering "Hooke's law is F = -k*x, with F in N, k in N/m and x in
# m." -- the formula restatement the device test called cheap. A criterion that the thing being
# replaced already passes cannot decide anything, and finding that out AFTER the run would have
# been fitting the criterion to the result.
#
# So D1 requires the answer to carry prose that is not the template. The template is recognisable
# by its unit clause: "with F in N, k in N/m and x in m", which is how EXPLAIN_CLOSE_* renders
# {u}. An answer is TEMPLATE-ONLY when removing that clause and the relation leaves nothing.
_UNITCLAUSE = re.compile(r",?\s*(with|where|and)\s+[A-Za-z_][A-Za-z0-9_]*\s+in\s+\S+.*$", re.I)
_RELSTATE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\s*=\s*\S+")


def is_template_only(a):
    """True when the answer is a relation plus a units clause and nothing else."""
    rest = _UNITCLAUSE.sub("", a).strip()
    rest = _RELSTATE.sub("", rest)
    rest = re.sub(r"[^A-Za-z]+", " ", rest)
    words = [w for w in rest.split() if len(w) > 2]
    # "Hooke's law is" / "To find kinetic energy" leave 3-4 short words; real prose leaves many more
    return len(words) <= 6


# A RELATION THE RECORD DOES NOT HAVE IS A FABRICATION, and my first D1 counted two of them as
# PASS because the predicate only asked "is this not the template". Measured on know1:
#
#   record F=-k*x   answer "F = k*x is how long that force is"     sign flipped
#   record F=-k*x   answer "because F = G*x is not a big Delta_t"  G invented
#
# Both are fluent, neither is the template, and both are wrong physics stated confidently. That is
# the failure the whole tool-augmented architecture exists to prevent, so the gate has to see it.
# Same idea as prov_call_unsourced, applied to the ANSWER span instead of the tool call.
sys.path.insert(0, str(ROOT / "tools/eval"))
import grade                             # ONE relation comparison, shared with score_arms


def _norm_rel(x):
    return grade._relnorm(x)


# A NUMERIC RIGHT-HAND SIDE IS A RESULT, NOT A RELATION CLAIM. "W = 30 J" reports what the tool
# returned; it does not assert that W equals 30 in general. The first version flagged it, so a
# CORRECT computed answer -- the one the device test called "kinda decent" -- was marked as
# fabricating a relation against its own record.
_RESULT_RHS = re.compile(r"^\s*[-+]?\d")


def states_wrong_relation(answer, formula):
    """True when the answer asserts a relation that is not the record's."""
    want = _norm_rel(formula)
    for m in re.finditer(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^.,;]{1,40})", answer):
        if _RESULT_RHS.match(m.group(2)):
            continue
        got = _norm_rel(m.group(0))
        if got and got not in want and want not in got:
            return True, m.group(0).strip()
    return False, ""


def rare_words(meaning, df, k=5):
    """Content words of the meaning that appear in <= k of all meanings. Copying the term is easy;
    carrying its rare words is what the HIT leg is actually asking for."""
    ws = {w for w in re.findall(r"[a-z]{4,}", meaning.lower()) if df.get(w, 0) <= k}
    return ws


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip().splitlines()[-1])
        return 2
    ckpt = sys.argv[1]
    ck = torch.load(ckpt, map_location="cpu", weights_only=False)
    # A MISMATCHED CHECKPOINT/TOKENIZER PAIR SCORES 0.0 AND READS AS A MODEL RESULT. prepare.py
    # retrains the tokenizer every run; 3,886 of 4,096 ids changed between two consecutive ones.
    tok_sha = ck.get("tok_sha")
    live = hashlib.sha256(pathlib.Path(
        os.environ.get("TOK", ROOT / "train/tok4096.json")).read_bytes()).hexdigest()[:16]
    if tok_sha and tok_sha != live:
        print(f"REFUSING: checkpoint tok_sha {tok_sha} != tokenizer on disk {live}. "
              f"Score it with TOK=<its archived tokenizer>; a mismatched pair returns 0.0 and "
              f"reads as a devastating model result.")
        return 2
    m = Transformer(ModelArgs(**ck["args"])); m.load_state_dict(ck["model"]); m.eval()

    train = json.load(open(ROOT / "corpus/knowledge/definitions_train.json"))
    hold = json.load(open(ROOT / "corpus/knowledge/definitions_holdout.json"))
    df = {}
    for d in train + hold:
        for w in set(re.findall(r"[a-z]{4,}", d["meaning"].lower())):
            df[w] = df.get(w, 0) + 1

    # ---- D1 the device transcript ----------------------------------------------------------
    print("D1  THE DEVICE TRANSCRIPT (primary). Greedy, one attempt, the device's own prompt.")
    d1 = d1_old = 0
    for q, kind in TRANSCRIPT:
        p = subprocess.run([str(ROOT / "build/devprompt"), str(ROOT / "build/store.tns"), q, "0"],
                           capture_output=True, text=True)
        pre = next((l for l in p.stdout.split("\n") if l.startswith("<q>")), None)
        if not pre:
            print(f"  FAIL {q!r}: no prompt"); continue
        out = gen(m, pre)
        a = answer_of(out)
        tmpl = is_template_only(a)
        rec_f = pre.split("</q><r>", 1)[1].split(" | ", 1)[0] if "</q><r>" in pre else ""
        wrong, badrel = states_wrong_relation(a, rec_f)
        base = bool(a) and "<end>" in out and not DECLINE.search(a) and not wrong
        # A COMPUTE question is graded on producing a NUMBER, not on producing prose.
        ok = base and (bool(re.search(r"\d", a)) if kind == "compute" else not tmpl)
        d1_old += base and not tmpl
        d1 += ok
        why = "" if ok else ("  <- DECLINED" if DECLINE.search(a) else
                             "  <- TEMPLATE ONLY, no explanation" if tmpl else
                             f"  <- FABRICATED RELATION {badrel!r} against {rec_f!r}" if wrong
                             else "  <- malformed")
        print(f"  {'PASS' if ok else 'FAIL'} {q!r}{why}\n       {a[:150]}")
    print(f"  D1 = {d1}/6   (PASS is >= 5)   "
          f"[grading all six as explain, as first written: {d1_old}/6]\n")

    # ---- D4 / D5 trained terms --------------------------------------------------------------
    items = sorted(train, key=lambda d: hashlib.sha256(
        ("kprobe/pick/v1:" + d["term"]).encode()).hexdigest())[:150]
    hit = ref = 0
    for d in items:
        term, mean = d["term"], d["meaning"]
        r = rng_for(term)
        q, _ = AE.ask(term, r, mangle_rate=0.0)
        q = KD.ask_agrees(q, term)
        a = answer_of(gen(m, f"<q>{q}</q><r>{KD.span(term, mean)}"))
        rw = rare_words(mean, df)
        got = {w for w in re.findall(r"[a-z]{4,}", a.lower())}
        if a and "<tool" not in a and not DECLINE.search(a) and len(rw & got) >= min(2, len(rw)):
            hit += 1
        q3 = r.choice(KD.K3_ASK).format(t=term)
        a3 = answer_of(gen(m, f"<q>{q3}</q><r>{KD.span(term, mean)}"))
        if a3 and DECLINE.search(a3):
            ref += 1
    print(f"D4  K1 HIT,    150 TRAINED terms  {hit}/150 = {100*hit/150:.1f}%   (band 60-95%)")
    print(f"D5  K3 REFUSE, the same 150       {ref}/150 = {100*ref/150:.1f}%   (floor 80%)")
    print("    D4 and D5 are ONE measurement: explain-everything scores 100/0, refuse-everything "
          "0/100.\n")

    # ---- D6 held out, reported and stratified, NEVER gated ----------------------------------
    hh = {"clean": [0, 0], "contaminated": [0, 0]}
    for d in hold:
        term, mean = d["term"], d["meaning"]
        r = rng_for(term)
        q, _ = AE.ask(term, r, mangle_rate=0.0)
        q = KD.ask_agrees(q, term)
        a = answer_of(gen(m, f"<q>{q}</q><r>{KD.span(term, mean)}"))
        rw = rare_words(mean, df)
        got = {w for w in re.findall(r"[a-z]{4,}", a.lower())}
        ok = bool(a) and "<tool" not in a and not DECLINE.search(a) and len(rw & got) >= min(2, len(rw))
        k = "clean" if (not d.get("in_train_defs") and not d.get("sec_freq")) else "contaminated"
        hh[k][0] += ok; hh[k][1] += 1
    tot = sum(v[0] for v in hh.values()); n = sum(v[1] for v in hh.values())
    print(f"D6  HELD OUT, REPORTED AND NOT GATED (predicted band 2-25%, prior 3.3%)")
    for k, (o, nn) in hh.items():
        print(f"      {k:13s} {o}/{nn}" + (f" = {100*o/nn:.1f}%" if nn else ""))
    print(f"      aggregate     {tot}/{n} = {100*tot/max(1,n):.1f}%  "
          f"-- n={n} gives about +-7 pp at p=0.10 and cannot resolve a 10-point effect.")
    print(f"      The aggregate mixes 'never seen' with 'seen in prose all chapter'; "
          f"the strata above are the number.")

    # ---- D7 REARRANGEMENT: does a new tool actually get REACHED FOR? -------------------------
    #
    # The corpus called `eval` 192,754 times and `solve` zero times until A93. The question this
    # arm answers is not "can the model rearrange" -- it cannot, and is not supposed to; the TOOL
    # rearranges. It is whether the model RECOGNISES a rearrangement request and emits a solve call
    # against the record it was shown.
    #
    # THREE THINGS ARE SCORED SEPARATELY AND NEVER SUMMED, because they fail for different reasons
    # and a single "correct" rate would hide which:
    #   called   it emitted <tool>solve   (did it reach for the right tool at all)
    #   targeted the call's relation is the record's, and the variable is the one asked for
    #   answered the prose states the rearranged relation the tool returned
    # A model can call solve on the wrong relation, or call it correctly and then ignore the result.
    #
    # THE ITEMS ARE DRAWN FROM THE STORE, NOT WRITTEN, so this cannot drift from what ships. Twelve
    # (record, variable) pairs, chosen by the same sha256 rule the other arms use so the set is
    # frozen without being hand-picked.
    store = json.load(open(ROOT / "corpus/store_clean.json"))
    pairs = [(r, v) for r in store for v in (r.get("units") or {})
             if v != r["f"].split("=", 1)[0].strip()]
    pairs.sort(key=lambda rv: hashlib.sha256(
        ("kprobe/r1/v1:" + rv[0]["f"] + ":" + rv[1]).encode()).hexdigest())
    called = targeted = answered = usable = 0
    for r, v in pairs:
        if usable >= D7_N:
            break
        want = subprocess.run([str(ROOT / "tools/eval/evalcli"),
                        f"<tool>solve<arg>{r['f']}<arg>{v}</tool>"],
                       capture_output=True, text=True).stdout
        # NOT `m`: that is the MODEL in this function, and shadowing it with a regex match made
        # the generation loop call a re.Match. Renamed rather than reordered, because the next
        # person to add an arm here will reach for `m` too.
        mm = re.search(r"<res>(.*?)</res>", want, re.S)
        if not mm or mm.group(1).startswith("!"):
            continue                      # !nosol: the tool refuses, so there is nothing to ask for
        want = mm.group(1)
        usable += 1
        q = f"solve {r['f']} for {v}"
        pr = subprocess.run([str(ROOT / "build/devprompt"), str(ROOT / "build/store.tns"), q, "0"],
                     capture_output=True, text=True)
        pre = next((l for l in pr.stdout.split("\n") if l.startswith("<q>")), None)
        if not pre:
            continue                      # retrieval found nothing; counted as a miss on all three
        out = gen(m, pre)
        a = answer_of(out)
        sq = _squash(out)
        if "<tool>solve<arg>" in sq:
            called += 1
            arg = sq.split("<tool>solve<arg>", 1)[1]
            got_f = arg.split("<arg>", 1)[0]
            got_v = arg.split("<arg>", 1)[1].split("</tool>", 1)[0] if "<arg>" in arg else ""
            if got_f == _squash(r["f"]) and got_v == _squash(v):
                targeted += 1
        if a and want.replace(" ", "") in a.replace(" ", ""):
            answered += 1
    print(f"\nD7  REARRANGEMENT, {usable} store pairs, greedy, the device's own prompt")
    print(f"      called solve   {called}/{usable}" + (f" = {100*called/usable:.1f}%" if usable else ""))
    print(f"      right target   {targeted}/{usable}" + (f" = {100*targeted/usable:.1f}%" if usable else ""))
    print(f"      stated result  {answered}/{usable}" + (f" = {100*answered/usable:.1f}%" if usable else ""))
    print(f"      These are NOT summed. A model can call the right tool on the wrong relation, or "
          f"call it correctly and ignore what it returns.")

    # ---- D8 CALCULUS: diff and integ, the other two tools with no training before A103 ---------
    #
    # Same three-way scoring as D7 and for the same reason. The shape differs in one way that
    # matters: diff and integ take the record's RIGHT-HAND SIDE, not the whole relation, because you
    # differentiate an expression and not an equation. A model that passes the whole formula has
    # made a real error and `targeted` is what catches it.
    cal_called = cal_targeted = cal_answered = cal_n = 0
    cpairs = []
    for r in store:
        if "=" not in r["f"]:
            continue
        lhs, rhs = r["f"].split("=", 1)
        for v in (r.get("units") or {}):
            if v != lhs.strip() and v in rhs:
                cpairs.append((r, lhs.strip(), rhs, v))
    cpairs.sort(key=lambda z: hashlib.sha256(
        ("kprobe/c1/v1:" + z[0]["f"] + ":" + z[3]).encode()).hexdigest())
    for r, lhs, rhs, v in cpairs:
        if cal_n >= D7_N:
            break
        w = subprocess.run([str(ROOT / "tools/eval/evalcli"),
                            f"<tool>diff<arg>{rhs}<arg>{v}</tool>"],
                           capture_output=True, text=True).stdout
        mw = re.search(r"<res>(.*?)</res>", w, re.S)
        if not mw or mw.group(1).startswith("!"):
            continue
        want = mw.group(1)
        cal_n += 1
        q = f"what is the derivative of {r['f']} with respect to {v}"
        pr = subprocess.run([str(ROOT / "build/devprompt"), str(ROOT / "build/store.tns"), q, "0"],
                            capture_output=True, text=True)
        pre = next((l for l in pr.stdout.split("\n") if l.startswith("<q>")), None)
        if not pre:
            continue
        out = gen(m, pre)
        a = answer_of(out)
        sq = _squash(out)
        if "<tool>diff<arg>" in sq:
            cal_called += 1
            arg = sq.split("<tool>diff<arg>", 1)[1]
            got_e = arg.split("<arg>", 1)[0]
            got_v = arg.split("<arg>", 1)[1].split("</tool>", 1)[0] if "<arg>" in arg else ""
            if got_e == _squash(rhs) and got_v == _squash(v):
                cal_targeted += 1
        if a and want.replace(" ", "") in a.replace(" ", ""):
            cal_answered += 1
    print(f"\nD8  CALCULUS (diff), {cal_n} store pairs, greedy, the device's own prompt")
    print(f"      called diff    {cal_called}/{cal_n}"
          + (f" = {100*cal_called/cal_n:.1f}%" if cal_n else ""))
    print(f"      right target   {cal_targeted}/{cal_n}"
          + (f" = {100*cal_targeted/cal_n:.1f}%" if cal_n else ""))
    print(f"      stated result  {cal_answered}/{cal_n}"
          + (f" = {100*cal_answered/cal_n:.1f}%" if cal_n else ""))
    print(f"      `right target` requires the RIGHT-HAND SIDE, not the whole relation: passing "
          f"the formula to diff is a real error and this is what sees it.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
