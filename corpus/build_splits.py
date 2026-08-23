#!/usr/bin/env python3
"""Build SELECT and REPORT from HELD frames.

A frame is a textbook question SHAPE. Filling its {q} slots with our quantity names and {n} slots
with values gives a question in genuinely independent phrasing that IS answerable from the attached
record -- so no alignment is needed, and the metric-inversion problem does not arise.

HELD frames split again 50/50 so SELECT and REPORT share no frame either."""
import json, re, random, hashlib
HELD=json.load(open("corpus/frames_held.json"))
FORMS=json.load(open("corpus/units_holdout.json"))
VAR=re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)(?![A-Za-z0-9_(])")
rng=random.Random(20260823)
rng.shuffle(HELD)
half=len(HELD)//2
SETS={"select":HELD[:half],"report":HELD[half:]}
sf={x["frame"] for x in SETS["select"]}; rf={x["frame"] for x in SETS["report"]}
assert not (sf & rf), "SELECT and REPORT share a frame"

# {q} slots play DIFFERENT ROLES in the original: "what is the {q} of the {q}" is
# "<quantity> of <object>", not two quantities. Filling every slot with a given produced
# "the capacitance of a parallel-plate capacitor and A of 5 of the d of 12". Slot 1 takes the
# quantity, later slots take a physical OBJECT, and the givens go in their own clause.
OBJECTS=["block","car","wire","circuit","spring","beam","disc","rod","cart","pendulum",
         "capacitor","coil","gas sample","lens","satellite","container","piston","wheel"]

def fill(frame, rec, rng):
    lhs,rhs=rec["f"].split("=",1)
    vs=sorted({v for v in VAR.findall(rhs)}-{"pi","e"})
    if not vs or "units" not in rec: return None
    vals={v: rng.choice([2,3,5,8,10,12,15,20,25,30]) for v in vs}
    qname=rec["name"].lower()
    if "worked example" in qname or len(qname) < 4: return None
    if frame.count("{q}") > 3: return None            # over-slotted frames cannot be filled cleanly
    obj=rng.choice(OBJECTS)
    out=[]; qi=0; ni=0
    for tok in frame.split():
        if tok=="{q}":
            out.append(qname if qi==0 else obj); qi+=1
        elif tok=="{n}":
            out.append(str(vals[vs[ni%len(vs)]])); ni+=1
        elif tok=="{u}": out.append("")
        else: out.append(tok)
    body=" ".join(t for t in out if t)
    body=re.sub(r"\s+([?.,;:])",r"\1",body).strip()
    # Givens always get their own clause, in textbook order: setup first, question second.
    setup="The "+obj+" has "+", ".join(f"{v} = {vals[v]}" for v in vs)+"."
    q=setup+" "+body[0].upper()+body[1:]
    if not q.rstrip().endswith(("?",".")): q=q.rstrip()+"?"
    um=" ".join(f"{v}:{rec['units'][v]}" for v in vs if v in rec["units"])
    return {"q":q,"record":f"{rec['f']} | {um} | standard conditions",
            "id":f"{rec['f'][:8]}|{hashlib.sha1(frame.encode()).hexdigest()[:6]}","calls":["x"]}

for name,frames in SETS.items():
    items=[]; i=0
    while len(items)<60 and i<len(frames)*8:
        fr=frames[i%len(frames)]["frame"]; rec=FORMS[i%len(FORMS)]
        it=fill(fr,rec,rng)
        if it and 30<len(it["q"])<300: items.append(it)
        i+=1
    json.dump(items, open(f"corpus/split_{name}.json","w"), indent=1)
    print(f"  {name.upper():<7} {len(items)} items from {len(frames)} frames x {len(FORMS)} held-out formulas")
sel=json.load(open("corpus/split_select.json")); rep=json.load(open("corpus/split_report.json"))
assert not ({x["q"] for x in sel} & {x["q"] for x in rep}), "SELECT/REPORT share a question"
devq={it["q"] for it in json.load(open("tools/eval/items.json"))}
assert not ({x["q"] for x in sel}|{x["q"] for x in rep}) & devq, "overlaps DEV"
print("  asserted: no shared frame, no shared question, no overlap with DEV")
print("\n  samples:")
for x in sel[:4]: print(f"    {x['q'][:130]}")
