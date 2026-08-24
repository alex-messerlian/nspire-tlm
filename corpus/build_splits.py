#!/usr/bin/env python3
"""Build SELECT and REPORT from the discriminating construction.

CONSTRUCTION. Each item is a VERBATIM textbook stem paired with a record that either fits or does
not, banded fit:high / fit:low. The model must discriminate; a model that answers everything scores
50% and so does a model that refuses everything. That is the property the earlier construction
lacked -- it saturated at 100% because the model confabulated on every mismatch.

SEPARATION, on three axes, all asserted at build time:
  formulas  15 held-out formulas each, disjoint             (never trained on)
  stems     disjoint slices of 52,804 OpenStax sentences    (never authored by me)
  phrasing  no DEV item text appears in either split
"""
import json, re, random, hashlib
import sys as _sys, pathlib as _pl
_sys.path.insert(0, str(_pl.Path(__file__).parent))
from atomic import write_json as _wj

VAR=re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)(?![A-Za-z0-9_(])")
RESV={"pi","e","sin","cos","tan","ln","log","sqrt","exp"}
norm=lambda x: re.sub(r"[()\s]","",x)

hold=[r for r in json.load(open("corpus/units_holdout.json")) if r.get("units")]
# Deduplicate BEFORE splitting: the same exercise sentence appears in several modules,
# so disjoint index slices are not disjoint sets. Caught by the build-time assertion.
stems=sorted({s for s in json.load(open("corpus/stems_all.json"))
              if 40<len(s)<200 and "{" not in s})
dev={it["q"] for it in json.load(open("tools/eval/items.json"))}
rng=random.Random(20260824)
rng.shuffle(hold); rng.shuffle(stems)
half=len(hold)//2
FORM={"SELECT":hold[:half], "REPORT":hold[half:2*half]}
STEM={"SELECT":stems[:2000], "REPORT":stems[2000:4000]}

def build(name, n_match=40, n_mismatch=40):
    forms, st = FORM[name], STEM[name]
    out=[]
    for i in range(n_match+n_mismatch):
        matched = i < n_match
        r = forms[i % len(forms)]
        other = forms[(i+1) % len(forms)]
        src = r if matched else other          # mismatched: record is for a DIFFERENT relation
        vs=sorted({v for v in VAR.findall(r["f"].split("=",1)[1]) if v not in RESV})
        if not vs: continue
        vals={v: rng.choice([2,3,5,8,10,12,20]) for v in vs}
        svs=sorted({v for v in VAR.findall(src["f"].split("=",1)[1]) if v not in RESV})
        um=" ".join(f"{v}:{src['units'][v]}" for v in svs if v in src.get("units",{}))
        q=st[i % len(st)].strip()+" "+", ".join(f"{v} = {vals[v]}" for v in vs)+"."
        rec=(f"{src['f']} | {um} | missing:none | "
             f"{src.get('req','standard conditions')} | fit:{'high' if matched else 'low'}")
        out.append({"id":f"{name[:3]}-{len(out)+1:03d}","q":q,"record":rec,
                    "expect":"answer" if matched else "refuse","calls":["x"] if matched else []})
    return out

S, R = build("SELECT"), build("REPORT")
# ---- build-time assertions: separation must be structural, not intended -------------------
sf={norm(r["f"]) for r in FORM["SELECT"]}; rf={norm(r["f"]) for r in FORM["REPORT"]}
assert not (sf & rf), "SELECT and REPORT share a formula"
assert not ({x["q"] for x in S} & {x["q"] for x in R}), "SELECT and REPORT share a question"
assert not ({x["q"] for x in S} & dev) and not ({x["q"] for x in R} & dev), "overlaps DEV"
assert not (set(STEM["SELECT"]) & set(STEM["REPORT"])), "stem slices overlap"
for x in S+R:
    assert "fit:" in x["record"] and "missing:" in x["record"], "split item out of distribution"
_wj("corpus/split_select.json", S, indent=1)
_wj("corpus/split_report.json", R, indent=1)
h=lambda xs: hashlib.sha1(json.dumps([x["q"] for x in xs]).encode()).hexdigest()[:8]
print(f"  SELECT {len(S):>3} items  ({sum(1 for x in S if x['expect']=='answer')} answer / "
      f"{sum(1 for x in S if x['expect']=='refuse')} refuse)  {len(sf)} formulas  hash {h(S)}")
print(f"  REPORT {len(R):>3} items  ({sum(1 for x in R if x['expect']=='answer')} answer / "
      f"{sum(1 for x in R if x['expect']=='refuse')} refuse)  {len(rf)} formulas  hash {h(R)}")
print(f"  assertions: disjoint formulas, disjoint questions, disjoint stem slices, no DEV overlap,")
print(f"              every item in-distribution under the current record format -- all passed")
