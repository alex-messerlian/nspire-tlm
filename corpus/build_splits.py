#!/usr/bin/env python3
"""Build SELECT and REPORT from the discriminating construction.

CONSTRUCTION. Each item is a VERBATIM textbook stem paired with a record that either fits or does
not, banded fit:high / fit:low. The model must discriminate; a model that answers everything scores
50% and so does a model that refuses everything. That is the property the earlier construction
lacked -- it saturated at 100% because the model confabulated on every mismatch.

SEPARATION, on three axes, all asserted at build time:
  formulas  15 formulas each, disjoint BETWEEN THE SPLITS
  stems     disjoint slices of 52,804 OpenStax sentences    (never authored by me)
  phrasing  no DEV item text appears in either split

*** CORRECTION. This block used to read "15 held-out formulas each, disjoint (NEVER TRAINED ON)".
The parenthesis was FALSE and was never asserted: the assertions below check SELECT-vs-REPORT
disjointness, question overlap, stem-slice overlap and DEV overlap, and none of those is "the model
never trained on this formula".

MEASURED: the generator emits 164 formulas, units_holdout.json holds 30, 21 overlap. 10 of SELECT's
13 formulas appear VERBATIM in the training corpus. SELECT is therefore mostly an IN-DISTRIBUTION
test that was documented as a generalisation test, and every number read off it inherits that --
including the retracted 16%.

To make the claim true, the holdout must be carved OUT of the store BEFORE generation, so that
"never trained on" holds by construction rather than by a file name. tools/eval/gate_split_heldout.py
records the current overlap and will start ENFORCING separation the moment it reaches zero. ***
"""
import json, re, random, hashlib
import sys as _sys, pathlib as _pl
_sys.path.insert(0, str(_pl.Path(__file__).parent))
from atomic import write_json as _wj

# Symbols the device resolves from the store rather than reading from the question. Derived from
# store_clean.json rather than hardcoded, so a new constant cannot silently start appearing as a
# "given" in a split item.
def _runtime_constants():
    S = json.load(open("corpus/store_clean.json"))
    S = S if isinstance(S, list) else S.get("records", [])
    out = set()
    for r in S:
        out |= set((r.get("cval") or {}).keys())
    assert out, "no cval symbols found in store_clean.json -- refusing to build a split blind"
    return out

def _rng_target(rng):
    """Same distribution corpus/generate.py A43i draws from, so eval and train match in shape."""
    return rng.choices((2, 3, 4, 5), weights=(0.16, 0.27, 0.30, 0.27))[0]


VAR=re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)(?![A-Za-z0-9_(])")
RESV={"pi","e","sin","cos","tan","ln","log","sqrt","exp"}
norm=lambda x: re.sub(r"[()\s]","",x)

# THE HOLDOUT WAS NEVER CLEANED. store_clean.json went through the lints; units_holdout.json did
# not, so 3 of its 30 records are MathML conversion garbage -- two Leibniz artifacts,
# Q=((d*V)/(d*t)) and F_l=-((d*U)/(d*l)) where `d` cancels, and one fused identifier, B*E=h*f_O
# whose real form is E_b=h*f_O. The split draws 15 records from 30, so they landed in the split the
# SELECTION RULE reads, and the model was scored on answering formulas that are not physics.
#
# THIRD INSTANCE of a correction that did not propagate: the cleaning reached store_clean.json,
# not units_train.json (fixed earlier), and not this. Filtered here AND gated, because a filter in
# one builder is exactly what the previous two instances looked like.
_LEIBNIZ = re.compile(r"\(d\*[A-Za-z_]")
_FUSED   = re.compile(r"^[A-Z]\*[A-Z][A-Za-z0-9_]*=")
def _clean(r):
    f = r.get("f", "")
    return bool(r.get("units")) and not _LEIBNIZ.search(f) and not _FUSED.match(f)
_raw = json.load(open("corpus/units_holdout.json"))
hold = [r for r in _raw if _clean(r)]
_dropped = [r["f"] for r in _raw if not _clean(r)]
if _dropped:
    print(f"  holdout cleaned: dropped {len(_dropped)} of {len(_raw)} -> {_dropped}")
# Deduplicate BEFORE splitting: the same exercise sentence appears in several modules,
# so disjoint index slices are not disjoint sets. Caught by the build-time assertion.
stems=sorted({s for s in json.load(open("corpus/stems_all.json"))
              if 40<len(s)<200 and "{" not in s})
dev={it["q"] for it in json.load(open("tools/eval/items.json"))}
# the generator itself, so the split's givens come from the declared windows rather than a
# hand-written list that no device question resembles
import importlib.util as _u, io as _io, contextlib as _ctx
_spec=_u.spec_from_file_location("_gen", "corpus/generate.py"); _gen=_u.module_from_spec(_spec)
with _ctx.redirect_stdout(_io.StringIO()): _spec.loader.exec_module(_gen)

_RUNTIME_CONSTANTS = _runtime_constants()
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
        # NEVER SUPPLY A VALUE FOR A CONSTANT THE RUNTIME INLINES. This assigned a small integer
        # to every RHS variable, so 32% of items said things like `c = 2` for the speed of light
        # and `h = 2` for Planck's -- and then scored the model WRONG for correctly inlining
        # c = 2.998e8, which is what the device does and what the corpus trains. The item was
        # out of distribution and the failure it produced was the model behaving correctly.
        # THE HOLDOUT RECORDS CARRY NO `cval` FIELD -- only the store's copies do -- so reading
        # r["cval"] here found nothing and 30 items still supplied `c`, `h` and `g`. The set of
        # symbols the runtime inlines is a property of the STORE, not of an individual record.
        _cv = _RUNTIME_CONSTANTS
        vs  = [v for v in vs if v not in _cv]
        if not vs: continue
        # GIVENS FROM THE SAME DISTRIBUTION THE RUNTIME PRODUCES, not seven small integers.
        #
        # This drew rng.choice([2,3,5,8,10,12,20]), so SELECT's questions carried SEVEN distinct
        # values and 98.8% of them were integers <= 20. In the training corpus such values are
        # 1.8% of all givens -- 4,446 distinct, median 9.259, spanning -566 to 5.4e29. The model
        # had essentially never seen a question shaped like "r = 12, omega = 2", so the answer side
        # of SELECT was measuring OUT-OF-DISTRIBUTION ROBUSTNESS and reporting it as record-reading.
        #
        # Fourth instrument defect on this one split, after removed records, overridden constants
        # and the false held-out claim. Same root each time: the split built items the DEVICE would
        # never emit. Drawing through the generator's own declared windows removes the whole class.
        vals={}
        for v in vs:
            rr = _gen.quantity_range(r, v)
            x  = _gen.sample_in_range(rng, rr[0], rr[1], rr[2]) if rr else _gen.sample_value(rng)
            vals[v] = _gen._num(x) if hasattr(_gen, "_num") else x
        # A43k. THE SPLIT GETS THE SAME SPARE-GIVEN TREATMENT AS TRAINING.
        #
        # A43 added spare givens and equalised the total count across classes in the TRAINING
        # corpus, and this file did not follow -- so eval questions carried 56.8% spare givens and
        # 2.14 of them on average against training's 78.8% and 3.75. distribution_gate caught it as
        # a rise in eval-vs-train separability (REPORT excess +43.3 pp against a 40.5 baseline):
        # the split had become distinguishable from training by question SHAPE, which is the same
        # class of defect as the ". Also " literal, one file over.
        #
        # Seventh instrument defect in this file and the same root as the other six: it constructs
        # items independently of the generator instead of through it.
        _target=_rng_target(rng)
        for _ in range(12):
            if len(vals) >= _target: break
            _o=rng.choice(_gen.recs)
            if _o["f"]==src["f"]: continue
            _c=[v for v in dict.fromkeys(VAR.findall(_o["f"].split("=",1)[1]))
                if v not in RESV and v not in vals and v not in VAR.findall(src["f"])
                and _gen._const_for(_o, v) is None]
            if not _c: continue
            _rr=_gen.quantity_range(_o,_c[0])
            _x=_gen.sample_in_range(rng,_rr[0],_rr[1],_rr[2]) if _rr else _gen.sample_value(rng)
            vals[_c[0]]=_gen._num(_x) if hasattr(_gen,"_num") else _x
        svs=sorted({v for v in VAR.findall(src["f"].split("=",1)[1]) if v not in RESV})
        um=" ".join(f"{v}:{src['units'][v]}" for v in svs if v in src.get("units",{}))
        # ITERATE vals, NOT vs. `vs` is the source record's own variables, so the A43k spares --
        # which live in `vals` -- never reached the question and the split's shape was unchanged.
        # A silent no-op: the code ran, the numbers did not move, and only re-measuring the shape
        # showed it.
        # A44. THE QUESTION MUST ASK FOR THE RECORD'S QUANTITY.
        #
        # This paired a SHUFFLED OpenStax sentence with a record by `i % len(st)` and appended the
        # record's values, so the question and the record were unrelated BY CONSTRUCTION:
        #
        #   Q "What is the cost savings for using the LED in place of the incandescent bulb..."
        #   R v_CM=r*omega | omega:1/s r:m | missing:none | ... | fit:high     expect: ANSWER
        #
        # Retrain 2 refuses 94.6% of those, which is CORRECT, and the arm scored it as failure --
        # so the model that improved scored worse. The SELECT answer metric has never measured
        # record-reading; every number it produced (58.8%, 47.5%, 20.0%, 3.6%) is on this shape.
        #
        # A6 one level up: mining question surfaces and substituting blind. The fix is the one the
        # generator and tools/eval/{fit_judgement,answer_control,d1_arm}.py already use -- ask for
        # the record's own quantity through the reviewed frames. It also makes the split's question
        # surface match training, which the OpenStax stems never did.
        #
        # Eighth instrument defect in this file, and the same root as the other seven: it built
        # items independently of the generator instead of through it.
        # AN `expect: answer` ITEM MUST BE FULLY BOUND. Three of 37 in each split carried
        # `missing:<var>` and were still labelled answerable -- the model refusing them is right,
        # and the arm scored it wrong. Fill anything the device would call missing.
        if matched:
            for _v in dict.fromkeys(VAR.findall(src["f"].split("=", 1)[1])):
                if _v in RESV or _v in vals or _gen._const_for(src, _v) is not None:
                    continue
                _rr = _gen.quantity_range(src, _v)
                _x = _gen.sample_in_range(rng, _rr[0], _rr[1], _rr[2]) if _rr else _gen.sample_value(rng)
                vals[_v] = _gen._num(_x) if hasattr(_gen, "_num") else _x
        _ord=list(vals); rng.shuffle(_ord)
        _ask=_gen.ask_for(src, _gen.quantity_surface(src, rng), rng)
        q=_gen.compose_question(_ask, ", ".join(f"{v} = {vals[v]}" for v in _ord), rng)
        # A38. A REAL RECORD IS ALWAYS fit:high. THE DEVICE HAS EXACTLY TWO SHAPES.
        #
        # src/store/assemble.c emits a real record with " | fit:high" and nothing else, or the
        # no-match literal with NO record. A real record carrying fit:low is a shape the runtime
        # CANNOT produce -- and this file emitted it for all 40 refuse items, so the refusal side
        # was scored entirely on prompts the device will never send.
        #
        # This is the A11 defect verbatim. The project log records it -- "D2 emitted a REAL RECORD with
        # fit:low, a shape the runtime can never produce, 616 of 11,975 documents" -- it was fixed
        # in corpus/generate.py, and never here. gate_fit_cue enforces it and never looked at the
        # splits, which is why it stayed. Fifth instrument defect in this file, same root as the
        # other four: constructing what the runtime cannot emit.
        #
        # The consequence is not cosmetic. On the device a BAD match still arrives as fit:high,
        # because the picker does not judge fit -- so a model that refuses from the LABEL has
        # learned a cue that is never set that way at serve time. It has to refuse from the
        # record's CONTENT not matching the question, which is the judgement being tested.
        # A41. THE missing: FIELD IS COMPUTED BY THE DEVICE, AND THIS FILE HARDCODED IT.
        #
        # Sixth instrument defect on this split, and the same root as the other five: a field the
        # runtime derives, written here as a constant. src/store/assemble.c fills `missing` with the
        # shown record's variables that the question does not supply, and corpus/generate.py calls
        # _device_missing for exactly that reason -- its A11 note says D1 and D2 "both emit
        # missing:<var>".
        #
        # Measured on the trained corpus: ANSWERABLE is missing:none in 100.0% of documents, D2 is
        # missing:<var> in 99.0%. This file wrote missing:none on 100% of refuse items, so in that
        # field every refuse item was byte-identical to the answerable class.
        #
        # It explains both scores and neither is a capability measurement. With the old fit:low the
        # refuse arm read 100% -- the model was matching a label D3 taught it. With fit:high and
        # missing:none it read 7.5% -- the model was reading the answerable signature and answering.
        # I was one step from publishing "the model cannot judge whether a record fits", which the
        # evidence does not support: the split had never put that judgement to it.
        miss = _gen._device_missing_mod(src, set(vals))
        rec=(f"{src['f']} | {um} | missing:{miss} | "
             f"{src.get('req','standard conditions')} | fit:high")
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
    # AN ANSWERABLE ITEM THAT IS NOT FULLY BOUND IS A REFUSAL ITEM WEARING THE WRONG LABEL.
    assert not (x["expect"] == "answer" and " missing:none " not in x["record"]), (
        f'{x["id"]} expects an answer but the record is missing a variable: {x["record"][:70]}')
_wj("corpus/split_select.json", S, indent=1)
_wj("corpus/split_report.json", R, indent=1)
h=lambda xs: hashlib.sha1(json.dumps([x["q"] for x in xs]).encode()).hexdigest()[:8]
print(f"  SELECT {len(S):>3} items  ({sum(1 for x in S if x['expect']=='answer')} answer / "
      f"{sum(1 for x in S if x['expect']=='refuse')} refuse)  {len(sf)} formulas  hash {h(S)}")
print(f"  REPORT {len(R):>3} items  ({sum(1 for x in R if x['expect']=='answer')} answer / "
      f"{sum(1 for x in R if x['expect']=='refuse')} refuse)  {len(rf)} formulas  hash {h(R)}")
print(f"  assertions: disjoint formulas, disjoint questions, disjoint stem slices, no DEV overlap,")
print(f"              every item in-distribution under the current record format -- all passed")
