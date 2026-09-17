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
TRANSCRIPT = ["what is hookes law", "newton second law", "explain hokes law",
              "what is kinetic energy", "find work when f=12 d=2.5", "what is acceleration"]


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


def states_wrong_relation(answer, formula):
    """True when the answer asserts a relation that is not the record's."""
    want = _norm_rel(formula)
    for m in re.finditer(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^.,;]{1,40})", answer):
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
    d1 = 0
    for q in TRANSCRIPT:
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
        ok = (bool(a) and "<end>" in out and not DECLINE.search(a) and not tmpl and not wrong)
        d1 += ok
        why = "" if ok else ("  <- DECLINED" if DECLINE.search(a) else
                             "  <- TEMPLATE ONLY, no explanation" if tmpl else
                             f"  <- FABRICATED RELATION {badrel!r} against {rec_f!r}" if wrong
                             else "  <- malformed")
        print(f"  {'PASS' if ok else 'FAIL'} {q!r}{why}\n       {a[:150]}")
    print(f"  D1 = {d1}/6   (PASS is >= 5)\n")

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
    return 0


if __name__ == "__main__":
    sys.exit(main())
