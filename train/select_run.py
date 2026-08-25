#!/usr/bin/env python3
"""One seed of the ship config, scored on SELECT only.

REPORT is never opened by this script. Selection rule is committed in docs/SELECTION_RULE.md:
top-1 on SELECT discrimination accuracy -- answered when fit:high, refused when fit:low.

Written clean rather than patched: accumulated edits had left a stale import, an unguarded
divergence gate and an undefined name, all three caught by a 600-step smoke test costing ~3 minutes
against 8 x 20 minutes of seeds."""
import os, sys, json, re, subprocess, time, statistics as st
import numpy as np, torch, pathlib as _pl
sys.path.insert(0,"vendor/llama2.c"); sys.path.insert(0,"corpus")
from model import Transformer, ModelArgs
from tokenizers import Tokenizer

SEED  = int(os.environ.get("SEED","1"))
STEPS = int(os.environ.get("STEPS","8000"))
V,DIM,LAYERS,HEADS,SEQ,BS,LR = 4096,288,6,6,256,24,3e-4
dev = "mps" if torch.backends.mps.is_available() else "cpu"
torch.manual_seed(SEED); np.random.seed(SEED)

# ---- pre-run checklist ------------------------------------------------------------------
import collections
docs=[json.loads(l) for l in open("corpus/synth_sample.jsonl")]
kinds=collections.Counter(d.get("kind","answer") for d in docs); tot=sum(kinds.values())
for kind,(lo,hi) in {"D1":(0.08,0.12),"D2":(0.03,0.07)}.items():
    share=kinds.get(kind,0)/tot
    assert lo<=share<=hi, f"corpus: {kind} is {share:.1%}, expected {lo:.0%}-{hi:.0%}"
assert any("fit:low" in d["text"] for d in docs), "corpus has no fit:low documents"
SEL=json.load(open("corpus/split_select.json"))
for i in SEL:
    assert "fit:" in i["record"] and "missing:" in i["record"], f"{i['id']} out of distribution"
ANS=[i for i in SEL if i["expect"]=="answer"]; REFI=[i for i in SEL if i["expect"]=="refuse"]
print(f"  seed {SEED}  corpus " + " ".join(f"{a} {100*b/tot:.0f}%" for a,b in kinds.most_common())
      + f"  SELECT {len(ANS)} answer / {len(REFI)} refuse", flush=True)

r=subprocess.run([".venv-tok/bin/python","train/prepare.py","4096"],capture_output=True,text=True)
assert r.returncode==0, f"prepare.py failed: {r.stderr[-300:]}"
tr=np.fromfile("train/mix4096_train.bin",dtype=np.uint16)
tk=Tokenizer.from_file("train/tok4096.json")
RES_O,RES_C,ENDT,TOOLC=(tk.token_to_id(t) for t in ("<res>","</res>","<end>","</tool>"))

# ---- train -----------------------------------------------------------------------------
import math
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
    msk=np.zeros_like(y)
    for rr in range(BS):
        ins=False
        for c in range(SEQ):
            if x[rr,c]==RES_O: ins=True
            elif x[rr,c]==RES_C: ins=False
            elif ins: msk[rr,c]=1
    xb=torch.from_numpy(x).to(dev); yb=torch.from_numpy(np.where(msk==1,-100,y)).to(dev)
    _=m(xb,yb); l=m.last_loss
    opt.zero_grad(set_to_none=True); l.backward()
    torch.nn.utils.clip_grad_norm_(m.parameters(),1.0); opt.step()
    win.append(l.item())
    if s and s%500==0: curve.append(sum(win)/len(win)); win.clear()
torch.save({"model":m.state_dict(),"args":args.__dict__,"seed":SEED,"steps":STEPS},
           f"train/sel_s{SEED}.pt")

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
@torch.no_grad()
def gen(it):
    ids=tk.encode(f"<q>{it['q']}</q><r>{it['record']}").ids
    x=torch.tensor([ids]).to(dev); g=[]
    m.eval()
    for _ in range(160):
        lg=m(x[:,-SEQ:])[:,-1,:]; lg[0,RES_O]=-1e30
        n=int(torch.multinomial(torch.softmax(lg/0.8,-1),1))
        x=torch.cat([x,torch.tensor([[n]]).to(dev)],1); g.append(n)
        if n==ENDT: break
        if n==TOOLC:
            c=re.search(r"<tool>.*?</tool>",tk.decode(g,skip_special_tokens=False),re.S)
            rr=run_call(c.group(0).replace(" ","")) if c else "!give"
            inj=tk.encode(f"<res>{rr}</res>").ids
            x=torch.cat([x,torch.tensor([inj]).to(dev)],1); g+=inj
    m.train(); return tk.decode(g,skip_special_tokens=False)
# A REFUSAL is correct only if it is well-formed and refuses -- same standard both sides.
def _ref_ok(i):
    o=gen(i); return well_formed(o) and bool(REF.search(o))
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

def _ans_ok(i):
    o=gen(i)
    return (well_formed(o) and not REF.search(o)
            and _answer_matches_result(o) and _prov_clean(o))
ans_ok=sum(_ans_ok(i) for i in ANS)
disc=(ref_ok+ans_ok)/(len(REFI)+len(ANS))
print(f"  SELECT discrimination {disc*100:.1f}%   "
      f"(refuse {100*ref_ok/len(REFI):.1f}%  answer {100*ans_ok/len(ANS):.1f}%)  "
      f"n={len(SEL)}", flush=True)
json.dump({"seed":SEED,"discrimination":disc,
           "refuse_correct":ref_ok/len(REFI),"answer_correct":ans_ok/len(ANS)},
          open(f"train/sel_s{SEED}.json","w"), indent=1)
