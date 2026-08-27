#!/usr/bin/env python3
"""Every relation the generator emits must be one the CLEANED store contains.

docs/RESULT_STORE_CLEANING.md deleted 34 records in four documented groups -- non-physics
recurrences, duplicates, and misnamed relations including `R_eqv=R_1-R_2` (resistances do not
subtract) and `v=lambda*f` labelled "speed of light" when it is wave speed. corpus/units_train.json
holds the UNIT annotations and was never re-cleaned, so it still carries all of them.

A change that made units_train the set generate.py iterates re-admitted 24 of the deleted records --
three named "Strategy", one named "Graficar una ecuacion polar", two whose name is their own
formula. The store-cleaning was silently undone by a change made two commits later, and nothing
noticed. This is the check that notices.

Exit 0 clean, 1 on a violation, 2 if it cannot run.
"""
import io, contextlib, importlib.util, json, os, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
os.chdir(ROOT)          # corpus/generate.py opens its inputs relative to the working directory
try:
    store = {r["f"] for r in json.load(open(ROOT / "corpus/store_clean.json"))}
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    gen = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(gen)
except Exception as e:                      # cannot check is not clean
    print(f"CANNOT CHECK: {type(e).__name__}: {e}")
    sys.exit(2)

if not gen.recs:
    print("CANNOT CHECK: the generator admitted zero records")
    sys.exit(2)

bad = [r for r in gen.recs if r["f"] not in store]
print(f"generator relations {len(gen.recs)}, all required to be in the cleaned store ({len(store)})")
if bad:
    print(f"{len(bad)} NOT IN THE CLEANED STORE:")
    for r in bad[:20]:
        print(f"  {r['f']:34} {r.get('name','')[:44]}")
    sys.exit(1)
print("clean")
