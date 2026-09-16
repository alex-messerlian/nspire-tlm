#!/usr/bin/env python3
"""One seed of the ship config, scored on SELECT only.

REPORT is never opened by this script. Selection rule is committed in docs/SELECTION_RULE.md:
top-1 on SELECT discrimination accuracy -- answered when fit:high, refused when fit:low.

Written clean rather than patched: accumulated edits had left a stale import, an unguarded
divergence gate and an undefined name, all three caught by a 600-step smoke test costing ~3 minutes
against 8 x 20 minutes of seeds."""
import os, pathlib, sys, json, re, subprocess, time, statistics as st
import numpy as np, torch, pathlib as _pl
sys.path.insert(0,"vendor/llama2.c"); sys.path.insert(0,"corpus"); sys.path.insert(0,"tools/eval")
sys.path.insert(0,"train")
import genloop                       # THE generation loop; never reimplement it
from model import Transformer, ModelArgs
from tokenizers import Tokenizer

SEED  = int(os.environ.get("SEED","1"))
STEPS = int(os.environ.get("STEPS","8000"))
# CONFIG FROM THE ENVIRONMENT, defaults unchanged. The d352 L6 C512 run needs a different shape
# and editing the constant would have made every prior run in the log ambiguous about which config
# produced it. docs/PREREG_TRAINING_RUN.md fixes the values for this run.
V     = int(os.environ.get("VOCAB",  "4096"))
DIM   = int(os.environ.get("DIM",    "288"))
LAYERS= int(os.environ.get("LAYERS", "6"))
HEADS = int(os.environ.get("HEADS",  "6"))
SEQ   = int(os.environ.get("SEQ",    "256"))
BS    = int(os.environ.get("BS",     "24"))
LR    = float(os.environ.get("LR",   "3e-4"))
assert DIM % HEADS == 0, f"dim {DIM} not divisible by heads {HEADS}"
dev = "mps" if torch.backends.mps.is_available() else "cpu"
torch.manual_seed(SEED); np.random.seed(SEED)

# ---- pre-run checklist ------------------------------------------------------------------
import collections
import corpus_check                  # ONE implementation of the composition check
docs, kinds = corpus_check.check()
tot=sum(kinds.values())
SEL=json.load(open("corpus/split_select.json"))
for i in SEL:
    assert "fit:" in i["record"] and "missing:" in i["record"], f"{i['id']} out of distribution"
ANS=[i for i in SEL if i["expect"]=="answer"]; REFI=[i for i in SEL if i["expect"]=="refuse"]
print(f"  seed {SEED}  corpus " + " ".join(f"{a} {100*b/tot:.0f}%" for a,b in kinds.most_common())
      + f"  SELECT {len(ANS)} answer / {len(REFI)} refuse", flush=True)

# The corpus size guard lives in train/corpus_check.py with the composition bands: both are
# facts about the COMPUTE tier, and keeping them apart is how one of them got the wrong denominator.

# A49. TWO ARTEFACTS THIS RUN IS ABOUT TO DESTROY, AND NEITHER LOSS ANNOUNCES ITSELF.
#
# 1. prepare.py can REPLACE train/tok4096.json. It no longer does so unconditionally -- it reuses an
#    existing table unless the vocab size changed or an <unk> probe shows it cannot represent the
#    corpus (train/prepare.py: "THE TOKENIZER IS REUSED WHEN AN EQUIVALENT ONE EXISTS. THIS IS THE
#    FIX, NOT THE CONTAINMENT") -- so the common case is a no-op and this archive costs nothing.
#    It is kept for the case that fix deliberately leaves open: a corpus that outgrows the table, or
#    a vocab change, both of which DO retrain it. A checkpoint trained against the old table then
#    scores 0.0 on every arm rather than erroring -- 3,886 of 4,096 ids changed between two
#    consecutive runs, back when it always retrained -- and the current table pairs with the ENTIRE
#    capability-cliff ladder (d320, d336, d352, d416), so the one time it does fire is the time the
#    project's headline finding would become unscoreable.
# 2. The checkpoint is written to train/sel_s{SEED}.pt, and SEED defaults to 1. sel_s1.pt held
#    d416. A rerun overwrites it silently.
#
# Both are converted from "remember to" into "the run refuses to". Archiving is keyed by CONTENT
# hash, so re-archiving an already-archived tokenizer is a no-op and cannot lose one.
import shutil as _sh, hashlib as _h0
_tokp = pathlib.Path("train/tok4096.json")
if _tokp.exists():
    _sha = _h0.sha256(_tokp.read_bytes()).hexdigest()[:16]
    _arch = pathlib.Path(f"train/tok4096_{_sha}.json")
    if not _arch.exists():
        _sh.copyfile(_tokp, _arch)
        print(f"  A49: archived the outgoing tokenizer {_sha} -> {_arch.name} "
              f"(prepare.py is about to replace it)", flush=True)
    else:
        print(f"  A49: outgoing tokenizer {_sha} already archived as {_arch.name}", flush=True)

RUN  = os.environ.get("RUN", f"sel_s{SEED}")
_out = pathlib.Path(f"train/{RUN}.pt")
assert not _out.exists(), (
    f"A49: {_out} already exists and this run would overwrite it. Set RUN=<name> to write "
    f"elsewhere, or move the existing checkpoint. A checkpoint is two hours of compute and the "
    f"only copy of a measured result; nothing here may clobber one silently.")

# A50: the corpus fact is captured BEFORE prepare.py tokenises it and before a single step runs.
import hashlib as _hl
_corpus_sha = _hl.sha256(open("corpus/synth_sample.jsonl","rb").read()).hexdigest()[:16]
_heads = {__import__("json").loads(l).get("head") for l in open("corpus/synth_sample.jsonl")}
_heads.discard(None)
print(f"  corpus_sha {_corpus_sha} ({len(_heads)} heads, stamped at START -- a regeneration during "
      f"this run cannot relabel the checkpoint)", flush=True)

r=subprocess.run([".venv-tok/bin/python","train/prepare.py","4096"],capture_output=True,text=True)
assert r.returncode==0, f"prepare.py failed: {r.stderr[-300:]}"
tr=np.fromfile("train/mix4096_train.bin",dtype=np.uint16)
tk=Tokenizer.from_file("train/tok4096.json")
RES_O,RES_C,ENDT,TOOLC=(tk.token_to_id(t) for t in ("<res>","</res>","<end>","</tool>"))

# ---- train -----------------------------------------------------------------------------
import math
from lossmask import masked_targets   # THE loss mask, TOOL_SPEC s1. One definition:
# eight trainers each carried a copy of this loop and every copy leaked 38.7% of each
# result span into the loss. See train/lossmask.py.
args=ModelArgs(dim=DIM,n_layers=LAYERS,n_heads=HEADS,n_kv_heads=HEADS,
               vocab_size=V,max_seq_len=SEQ,dropout=0.0)
m=Transformer(args).to(dev)
opt=torch.optim.AdamW(m.parameters(),lr=LR,betas=(0.9,0.95),weight_decay=0.1)
WARM=max(1,int(STEPS*0.03))
def lr_at(s):
    if s<WARM: return LR*s/WARM
    p=(s-WARM)/max(1,STEPS-WARM); return 0.1*LR+0.9*LR*0.5*(1+math.cos(math.pi*p))
curve=[]; win=[]; t0=time.time()
for s in range(STEPS):
    for gp in opt.param_groups: gp["lr"]=lr_at(s)
    i=np.random.randint(0,len(tr)-SEQ-1,BS)
    x=np.stack([tr[j:j+SEQ] for j in i]).astype(np.int64)
    y=np.stack([tr[j+1:j+1+SEQ] for j in i]).astype(np.int64)
    xb=torch.from_numpy(x).to(dev); yb=torch.from_numpy(masked_targets(y, RES_O, RES_C)).to(dev)
    _=m(xb,yb); l=m.last_loss
    opt.zero_grad(set_to_none=True); l.backward()
    torch.nn.utils.clip_grad_norm_(m.parameters(),1.0); opt.step()
    win.append(l.item())
    if s and s%500==0: curve.append(sum(win)/len(win)); win.clear()
# THE CHECKPOINT RECORDS THE CORPUS IT SAW. Without this, a harness has to guess -- and
# train/remeasure.py guessed by importing corpus/generate.py, so when the generator went from 78
# relations to 141 the stratification silently began labelling relations TRAINED that this
# checkpoint never saw. A metric whose ground truth drifts from the artefact reports a different
# quantity under the same name.
# A50. STAMPED AT START, NOT AT SAVE. This read corpus/synth_sample.jsonl HERE -- after training,
# minutes-to-hours after the data was actually consumed -- so any regeneration during a run silently
# relabels the checkpoint with a corpus it never saw.
#
# MEASURED, on my own run: d416 saved at 01:16 and I regenerated the corpus at 01:12, so
# train/cliff_d416.pt is stamped 1239d94df710be63 -- the A46 corpus, which contains an EXPLAIN class
# that d416's own startup line shows it did not have. The corpus it DID train on is unrecoverable.
# I then read that stamp as evidence the pre-registration's control had been violated and wrote it
# into docs/RESULT_D416.md as a finding. The stamp exists precisely so an artefact carries the facts
# a harness needs; computed at the wrong moment it carries a falsehood instead, which is worse than
# carrying nothing, because nothing invites a check.
#
# Same class as the derived-field-goes-stale rule, with the twist that the staleness is created by
# the run itself. A fact about an INPUT is recorded when the input is read.
# (_corpus_sha and _heads are stamped at START -- see A50 above prepare.py)
# STAMP THE TOKENIZER, NOT ONLY THE CORPUS. prepare.py retrains the tokenizer every run, so a
# checkpoint is only scoreable with the one it was trained against -- 3,886 of 4,096 ids changed
# between two consecutive runs, and the mismatch scores 0.0 on every arm rather than erroring.
import hashlib as _hl
_tok_sha = _hl.sha256(open("train/tok4096.json","rb").read()).hexdigest()[:16]
torch.save({"model":m.state_dict(),"args":args.__dict__,"seed":SEED,"steps":STEPS,
            "corpus_sha":_corpus_sha,"corpus_heads":sorted(_heads),"tok_sha":_tok_sha,},
           str(_out))

# ---- divergence gate, BEFORE metrics ----------------------------------------------------
print("  loss: "+" ".join(f"{v:.4f}" for v in curve), flush=True)
if len(curve)>=4:
    q=len(curve)//4
    best=min(st.mean(curve[i:i+q]) for i in range(0,len(curve)-q+1))
    last=st.mean(curve[-q:]); sd=st.stdev(curve)
    if last>best+sd:
        print(f"  DIVERGED best={best:.4f} last={last:.4f} sd={sd:.4f} -- metrics NOT computed",
              flush=True); sys.exit(2)
    print(f"  converged best={best:.4f} last={last:.4f}", flush=True)

# ---- score on SELECT --------------------------------------------------------------------
REF=re.compile(r"\bcannot\b|\bcan't\b|\bnot given\b|\bnot enough\b|\bmissing\b|\bdoes not apply\b",re.I)
WF=re.compile(r"<a>.*<end>", re.S)
def well_formed(o):
    if not WF.search(o): return False
    if o.count("<tool>")!=o.count("</tool>"): return False
    return not (o.count("<tool>") and o.count("<arg>")<o.count("<tool>"))
assert well_formed("<tool> e<arg> 1</tool><res> 1</res><a> 1.<end>")
assert not well_formed("<tool> e<arg> 1</tool><res> 1</res> 1.<end>")
def run_call(t):
    p=subprocess.run(["tools/eval/evalcli",t],capture_output=True,text=True)
    mm=re.search(r"<res>(.*?)</res>",p.stdout,re.S); return mm.group(1) if mm else "!give"
# THE SHARED LOOP, imported. This file carried its own copy, and the copy is where the <res> ban,
# the injection and the halt condition each had to be right independently. genloop.py is the one
# implementation; tools/eval/test_genloop.py asserts the ban and refuses a reimplementation.
def _step(ids):
    with torch.no_grad():
        return m(torch.tensor([ids]).to(dev))[:, -1, :][0]

def _sample(lg):
    return int(torch.multinomial(torch.softmax(lg/0.8, -1), 1))

def gen(it):
    pre = f"<q>{it['q']}</q><r>{it['record']}"
    m.eval()
    g = genloop.generate_text(_step, lambda t: tk.encode(t).ids,
                              lambda i: tk.decode(i, skip_special_tokens=False),
                              tk.encode(pre).ids,
                              res_id=RES_O, end_id=ENDT, toolc_id=TOOLC,
                              run_tool=lambda c: (run_call(c.replace(" ", "")), 0.0),
                              max_tokens=160, sample=_sample, ctx=SEQ)
    m.train()
    # RETURNS BOTH SPANS. It used to return the generation alone, and _prov_clean was handed that
    # -- so provcli found no </q>, returned its cannot-check sentinel -1, and EVERY answer scored
    # provenance-unclean. All nine stored sel_s*.json read answer_correct 0.0 and discrimination
    # exactly 0.500; the eight-seed selection that picked the shipping checkpoint was a nine-way
    # tie on a metric that was measuring nothing. The project log records this exact bug -- "all 8 seeds
    # reported exactly 50.0% on SELECT ... it was a grader scope bug" -- so the diagnosis landed and
    # the fix did not. Scope is part of the contract: is_refusal wants the GENERATION, provenance
    # and shape want the FULL DOCUMENT, and no single string satisfies both.
    return pre, g
# A REFUSAL is correct only if it is well-formed and refuses -- same standard both sides.
def _ref_ok(i):
    _,o=gen(i); return well_formed(o) and bool(REF.search(o))
ref_ok=sum(_ref_ok(i) for i in REFI)
# An ANSWER is correct only if it is a well-formed document that does not refuse. Scoring on
# the absence of refusal words alone counted rambling garbage as correct -- a 600-step model
# emitting '<tool> eval<arg>(4.0)*(30.0)</tool>ike is 84.0)</tool>05.4)...' scored 100%.
NUM=re.compile(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?")
def _sig_round(v, sig):
    if v==0: return 0.0
    import math as _m
    mgn=10.0**(sig-1-_m.floor(_m.log10(abs(v))))
    return _m.floor(abs(v)*mgn+0.5)/mgn*(1 if v>0 else -1)
def _answer_matches_result(o):
    """The stated number must trace to the injected result, at the answer's own precision.
    Invariant 2. Without it a well-formed document with a fabricated number scores as correct --
    observed directly: <res>20</res> followed by 'is 3', <res>196</res> followed by 'gives 3600'."""
    res=re.findall(r"<res>(.*?)</res>", o, re.S)
    if not res: return False
    seg=o.split("<a>",1)[1] if "<a>" in o else ""
    vals=[]
    for tok in NUM.findall(seg):
        d=sum(1 for ch in tok.split("e")[0] if ch.isdigit())
        try: vals.append((float(tok), max(1,d)))
        except ValueError: pass
    refs=[]
    for r in res:
        mm=NUM.search(r)
        if mm:
            try: refs.append(float(mm.group()))
            except ValueError: pass
    return any(_sig_round(rv,sg)==_sig_round(v,sg) for v,sg in vals for rv in refs)

PROV="tools/eval/provcli"
def _prov_clean(doc):
    """Invariant 2, via the reference implementation rather than a second copy of it.
    Complementary to _answer_matches_result, not redundant -- measured on constructed cases:
    provenance passes an answer built on INVENTED CALL ARGUMENTS; result-match passes an answer
    that merely ECHOES A NUMBER FROM THE QUESTION and never uses the computation. Selection
    requires both. Does NOT verify: that the tool CHOSEN was the right one, or that the formula
    applied was the right formula -- only that numbers trace and the result was used."""
    r=subprocess.run([PROV],input=doc,capture_output=True,text=True)
    if r.returncode!=0: raise RuntimeError(f"provcli failed: {r.stderr[-200:]}")
    m=re.findall(r"=(-?\d+)", r.stdout)          # SIGNED: -1 is a sentinel, not a count
    if len(m)!=2: raise RuntimeError(f"provcli output unparseable: {r.stdout!r}")
    a,c=(int(v) for v in m)
    # -1 means "no <a> span, cannot check" -- a distinct state from "clean". Treating it as 0
    # would score every malformed document as provenance-clean, which is the failure this
    # whole sweep is about. Anything non-zero, including the sentinel, is not clean.
    return a==0 and c==0

SHAPE="tools/eval/shapecli"
def _shape_status(doc):
    """FULL DOCUMENT. docs/ARCHITECTURE.md s6. Three-valued: 'mismatch' disqualifies, 'unchecked'
    does not -- there is no shape rule for diff/integ/evalat/stat yet -- but 'unchecked' is counted
    and printed, so a rising unchecked fraction cannot quietly inflate the numerator."""
    r=subprocess.run([SHAPE],input=doc,capture_output=True,text=True)
    if r.returncode!=0: raise RuntimeError(f"shapecli failed: {r.stderr[-200:]}")
    mm=re.match(r"shape=(\w+)", r.stdout)
    if not mm: raise RuntimeError(f"shapecli output unparseable: {r.stdout!r}")
    return mm.group(1)

SHAPES=collections.Counter()
def _ans_ok(i):
    pre,o=gen(i)
    doc=pre+o
    if not (well_formed(o) and not REF.search(o) and _answer_matches_result(o)): return False
    if not _prov_clean(doc): return False
    st=_shape_status(doc); SHAPES[st]+=1
    return st!="mismatch"

# POSITIVE CONTROL, before the model is scored on anything.
#
# This is what would have caught the scope bug the turn it was introduced instead of nine seeds
# later: a hand-written document that is correct by construction must score correct. A grader that
# returns 0.0 for everything is indistinguishable from a model that answers nothing, and the JSON it
# writes looks like a result. tools/eval/positive_control.py already found five metrics where
# "working" and "measuring nothing" were the same number; this puts one in the selection path.
_CP=("<q>A sled goes 84 m in 7 s. d = 84, t = 7.</q>"
     "<r>v=d/t | v:m/s d:m t:s | missing:none | constant speed | fit:high")
_CG="<tool>eval<arg>(84)/(7)</tool><res>12</res><a> The speed is 12 m/s.<end>"
assert well_formed(_CG) and not REF.search(_CG), "positive control: form"
assert _answer_matches_result(_CG),              "positive control: result-match"
assert _prov_clean(_CP+_CG),  "POSITIVE CONTROL FAILED: provenance rejects a correct document. " \
                              "Check the SCOPE being passed before trusting any score below."
assert _shape_status(_CP+_CG)=="ok", "POSITIVE CONTROL FAILED: shape rejects a correct document."
_CW="<tool>eval<arg>(84)*(7)</tool><res>588</res><a> The speed is 588 m/s.<end>"
assert _shape_status(_CP+_CW)=="mismatch", "NEGATIVE CONTROL FAILED: shape accepts a wrong call."
print("  positive+negative control: the grader distinguishes a correct document from a wrong one",
      flush=True)

ans_ok=sum(_ans_ok(i) for i in ANS)
disc=(ref_ok+ans_ok)/(len(REFI)+len(ANS))
print(f"  SELECT discrimination {disc*100:.1f}%   "
      f"(refuse {100*ref_ok/len(REFI):.1f}%  answer {100*ans_ok/len(ANS):.1f}%)  "
      f"n={len(SEL)}", flush=True)
print(f"  shape: ok={SHAPES['ok']} mismatch={SHAPES['mismatch']} unchecked={SHAPES['unchecked']}"
      f"   (unchecked is NOT a pass -- see docs/ARCHITECTURE.md s6)", flush=True)
json.dump({"seed":SEED,"discrimination":disc,
           "refuse_correct":ref_ok/len(REFI),"answer_correct":ans_ok/len(ANS),
           "shape_ok":SHAPES["ok"],"shape_mismatch":SHAPES["mismatch"],
           "shape_unchecked":SHAPES["unchecked"]},
          open(f"train/sel_s{SEED}.json","w"), indent=1)
