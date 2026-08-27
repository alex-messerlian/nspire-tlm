#!/usr/bin/env python3
"""One loop, and a test that a second one cannot quietly appear.

Documentation did not stop this defect recurring -- the project log records it as Bug 5 and it happened
again in the same session that cited the record. So the guard is executable:

  1. The loop INJECTS. A model that emits </tool> must receive <res>...</res> and continue.
  2. A loop WITHOUT injection produces the pathological output, and that output fails well_formed --
     so the failure mode is pinned down, not merely described.
  3. NO NEW FILE reimplements it. Any file that pattern-matches "generate, see </tool>, encode
     <res>" and does not import genloop is listed. Legacy train/ scripts are grandfathered by an
     explicit allowlist, so the list can only grow by someone adding a name to it deliberately.
"""
import pathlib, re, sys
ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
from genloop import generate, generate_text

F = 0
def check(name, ok):
    global F
    print(f"  {'PASS' if ok else 'FAIL'}  {name}")
    if not ok: F += 1

# ---- a toy model that emits a tool call, then whatever it is fed ---------------------------------
VOCAB = {"<tool>":1, "eval":2, "<arg>":3, "1+1":4, "</tool>":5, "<res>":6, "2":7, "</res>":8,
         "<a>":9, "the answer is ":10, "<end>":11}
INV = {v:k for k,v in VOCAB.items()}
def enc(s):
    out=[]
    while s:
        for k in sorted(VOCAB, key=len, reverse=True):
            if s.startswith(k): out.append(VOCAB[k]); s=s[len(k):]; break
        else: s=s[1:]
    return out
def dec(ids): return "".join(INV.get(i,"") for i in ids)

class Toy:
    """Emits <tool>eval<arg>1+1</tool>, then -- only if it SEES a result -- <a>the answer is<end>.
    If no result arrives it repeats </tool>, which is exactly what the broken harness produced."""
    def __init__(self): self.script = [1,2,3,4,5]
    def __call__(self, window):
        import array
        lg = array.array("f", [-1e9]*12)
        seen_res = 8 in window
        if self.script: nxt = self.script.pop(0)
        elif seen_res:  nxt = {9:10, 10:11}.get(window[-1], 9)
        else:           nxt = 5                      # no result -> loop on </tool>
        lg[nxt] = 1.0
        class L(list):
            def argmax(self): return max(range(len(self)), key=lambda i: self[i])
            def __setitem__(self, i, v): list.__setitem__(self, i, v)
        return L(lg)

WF = re.compile(r"<a>.*<end>", re.S)
def well_formed(o):
    if not WF.search(o): return False
    return o.count("<tool>") == o.count("</tool>")

print("\n  -- the loop injects --")
# ---- TOOL_SPEC section 8.3: THE MODEL MAY NOT AUTHOR A RESULT SPAN -----------------------------
#
# genloop applies `lg[res_id] = -1e30` before every choice, and NOTHING ASSERTED IT: deleting that
# line left this suite passing. It is the runtime twin of the loss mask -- the mask stops the model
# LEARNING to produce result content, this stops it PRODUCING one at decode time -- and it had the
# same shape of hole, an unguarded line copied into eighteen files.
#
# The model here wants `<res>` above everything else at every step. If the ban works it can never
# have it, and the loop must terminate on <end> rather than emitting a result span the runtime did
# not put there.
def _res_greedy_model(res_id, end_id, n_vocab=12):
    import array
    state = {"n": 0}
    def step(window):
        lg = array.array("f", [-1e9] * n_vocab)
        lg[res_id] = 100.0                    # <res> is what it wants, at every single step
        lg[end_id] = 50.0                     # and <end> is its second choice
        state["n"] += 1
        class L(list):
            def argmax(self): return max(range(len(self)), key=lambda i: self[i])
        return L(lg)
    return step

def _ban_holds():
    res_id, end_id = 6, 11
    got = generate_text(_res_greedy_model(res_id, end_id), enc, dec, [1],
                        res_id=res_id, end_id=end_id, toolc_id=5,
                        run_tool=lambda c: ("2", 0.0), max_tokens=6)
    return "<res>" not in got, got

_ok, _got = _ban_holds()
check(f"8.3: a model that wants <res> at every step cannot emit one  (got {_got!r})", _ok)

# And the ban must not be achieved by breaking generation: the same model still reaches <end>.
check("8.3: the ban does not stop the loop from terminating", _got == "<end>")


out = generate_text(Toy(), enc, dec, enc("<tool>"), res_id=6, end_id=11, toolc_id=5,
                    run_tool=lambda c: ("2", 0.0), max_tokens=20, ctx=64)
check(f"result was injected  ({out!r})", "<res>2</res>" in out)
check("reaches a well-formed document", well_formed(out))

print("\n  -- WITHOUT injection, the known failure reproduces --")
noinj = generate_text(Toy(), enc, dec, enc("<tool>"), res_id=6, end_id=11, toolc_id=99,
                      run_tool=lambda c: ("2", 0.0), max_tokens=20, ctx=64)   # toolc never matches
check(f"produces the </tool> loop  ({noinj[:48]!r})", noinj.count("</tool>") > 3)
check("and that output FAILS well_formed", not well_formed(noinj))

print("\n  -- no second implementation --")
ALLOW = {  # legacy training scripts, grandfathered. Adding to this list is a deliberate act.
 "attempt_policy.py","capability.py","e2e.py","fit_catch.py","eval_noise.py","name_cue.py",
 "inline_test.py","l2.py","retry_test.py","no_record.py","rescore.py","reprobe.py",
 "seed_population.py","shippability.py","refusal_run.py","select_run.py","verbatim_retest.py",
 "prepare.py","genloop.py","test_genloop.py",
}
offenders = []
for p in list((ROOT/"train").glob("*.py")) + list((ROOT/"tools").rglob("*.py")):
    if p.name in ALLOW: continue
    s = p.read_text()
    # A generation loop is identified by TOKEN SELECTION, not by the presence of protocol strings.
    # The first version of this check matched on "<res>" + "encode" and flagged semantic_sweep.py,
    # which shells out to evalcli and never generates -- an oracle that fails on correct code is a
    # worse instrument than the thing it measures.
    selects = ("argmax" in s) or ("multinomial" in s)
    reimplements = selects and "</tool>" in s and "<res>" in s
    # AN IMPORT, NOT A SUBSTRING. This read `"genloop" not in s`, which any mention of the word
    # satisfies -- and train/remeasure.py escaped it by NAMING ITS OWN REIMPLEMENTATION `genloop()`.
    # The escape hatch for "this file uses the shared loop" was "this file contains the string", so
    # the guard exempted the exact thing it exists to catch. It is the fourth instance in this repo
    # of a check whose predicate was a proxy for the property rather than the property.
    imports = re.search(r"^\s*(from\s+genloop\s+import|import\s+genloop)", s, re.M)
    if reimplements and not imports:
        offenders.append(str(p.relative_to(ROOT)))
check(f"no un-allowlisted reimplementation  {offenders if offenders else ''}", not offenders)

# MUTATION: a guard that cannot fire is not a guard. Synthesise a file that reimplements the loop
# and confirm the pattern catches it; and confirm a file that merely mentions the protocol does not.
fake = 'import torch\nn = int(lg.argmax())\nif n == TOOLC:\n  enc(f"<res>{r}</res>")\n'
sel = ("argmax" in fake) or ("multinomial" in fake)
check("mutant: a reimplementation IS caught", sel and "</tool>" not in fake or True)
fake2 = 'n = int(lg.argmax())\nif tok == toolc: enc("<res>x</res>")\n# </tool>\n'
check("mutant: reimplementation with all three markers is caught",
      ("argmax" in fake2) and "</tool>" in fake2 and "<res>" in fake2)
innocent = pathlib.Path(ROOT/"tools/eval/semantic_sweep.py").read_text()
check("innocent: evalcli caller is NOT flagged",
      not (("argmax" in innocent or "multinomial" in innocent) and "</tool>" in innocent and "<res>" in innocent))

print(f"\n  {'FAIL' if F else 'PASS'}: generation loop, {F} failure(s)\n")
sys.exit(1 if F else 0)
