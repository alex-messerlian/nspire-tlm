#!/usr/bin/env python3
"""The generator's constant table and the store's cval must agree, per (record, variable).

ONE FACT IN TWO FILES. corpus/generate.py's _MICRO_CVAL says "this variable is supplied, not asked
for", and the generator's _device_missing skips it -- so a question that omits it still trains on
`missing:none`. src/store/assemble.c makes the same decision from the STORE's `cval`. When the two
disagree the model trains on missing:none and the device serves missing:<var>, in the same field
A41 was written to fix.

Measured when this gate was written: 7 of 10 _MICRO_CVAL entries had no cval in the store --
the electron mass and elementary charge on the cyclotron and radius records, the carrier density,
the Rydberg. gate_record_bytes caught one of them as a single byte mismatch, which is what a
0.2%-rate skew looks like from a sample; this gate compares the tables directly and sees all seven.

Fourth instance of the propagation class, after units_train, build/store.tns and units_holdout.
"""
import contextlib, importlib.util, io, json, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    g = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(g)
    mc = getattr(g, "_MICRO_CVAL", {})
    st = json.loads((ROOT / "corpus/store_clean.json").read_text())
    recs = st if isinstance(st, list) else st.get("records", st)
    byf = {r["f"]: r for r in recs}

    missing, mismatched, unknown = [], [], []
    for (f, v), spec_ in mc.items():
        r = byf.get(f)
        if r is None:
            unknown.append((f, v)); continue
        val = spec_[0] if isinstance(spec_, (list, tuple)) else spec_
        cv = (r.get("cval") or {})
        if v not in cv:
            missing.append((f, v, val))
        elif abs(float(cv[v]) - float(val)) > 1e-6 * max(1.0, abs(float(val))):
            mismatched.append((f, v, cv[v], val))
    # the other direction: a store cval the generator does not know is fine -- the device inlines
    # it and the generator never draws it -- but it is COUNTED, because silence is not a check.
    extra = sum(1 for r in recs for v in (r.get("cval") or {}) if (r["f"], v) not in mc)

    print(f"  generator _MICRO_CVAL entries: {len(mc)}   store cval entries the generator "
          f"does not declare: {extra} (benign, the device inlines them)")
    for f, v, val in missing:
        print(f"  MISSING from the store: {v} = {val} in {f[:52]}")
    for f, v, a, b in mismatched:
        print(f"  DIFFERENT value: {v} in {f[:40]} -- store {a}, generator {b}")
    for f, v in unknown:
        print(f"  declares a record the store does not have: {v} in {f[:52]}")
    bad = missing + mismatched + unknown
    if bad:
        print(f"FAIL: {len(bad)} constant(s) disagree. The model would train on missing:none "
              f"where the device serves missing:<var>.")
        return 1
    print("PASS: every generator constant is declared in the store with the same value")
    return 0


if __name__ == "__main__":
    sys.exit(main())
