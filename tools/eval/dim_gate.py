#!/usr/bin/env python3
"""Dimensional consistency gate for records. Uses the evaluator's own unit machinery, which the
record pipeline was not using at all -- found by gate_mutation.py, where v=d+t, F=m/a and
K=0.5*m*v passed every existing gate.

Method: substitute 1 <unit> for each variable and ask conv to express the RHS in the LHS's unit.
Dimensional mismatch surfaces as !units. Symbols not in the table are reported SEPARATELY from
dimensional failures -- 'I do not know this symbol' and 'this physics is wrong' are different
findings and conflating them would hide both."""
import json, re, subprocess, sys, collections

U = {  # standard physics symbol conventions. Hand-curated; this is the real cost of the gate.
 "v":"m/s","u":"m/s","c":"m/s","d":"m","s":"m","x":"m","y":"m","z":"m","r":"m","R":"m","L":"m",
 "h":"m","l":"m","w":"m","t":"s","T":"s","m":"kg","M":"kg","F":"N","W":"J","K":"J","U":"J",
 "E":"J","Q":"J","P":"W","p":"kg*m/s","a":"m/s^2","g":"m/s^2","A":"m^2","V":"m^3","rho":"kg/m^3",
 "f":"Hz","lambda":"m","k":"N/m","tau":"N*m","I":"A","q":"C","C":"F","B":"T","N":"1","n":"1",
 "mu":"1","eta":"1","theta":"1","phi":"1","alpha":"1","omega":"1",
}
SUB = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)(?![A-Za-z0-9_(])")
RESV = {"pi","e","sin","cos","tan","ln","log","sqrt","exp"}

def base(v):                       # v_0, T_F, m_1 -> v, T, m
    return v.split("_")[0]

def check(f):
    if "=" not in f: return ("skip", "no equation")
    lhs, rhs = f.split("=", 1)
    vs = {v for v in SUB.findall(f) if v not in RESV}
    unknown = sorted({v for v in vs if base(v) not in U})
    if unknown: return ("unknown", ",".join(unknown[:4]))
    if base(lhs) not in U: return ("unknown", lhs)
    sub = lambda x: SUB.sub(lambda m: f"(1 {U[base(m.group(1))]})" if m.group(1) not in RESV
                            else m.group(1), x)
    return ("call", f"<tool>conv<arg>{sub(rhs)}<arg>{U[base(lhs)]}</tool>")

# The original __main__ lived here. Removed: it ran `./evalcli` and
# `../../corpus/records_raw.json` -- hardcoded RELATIVE paths that only worked from one
# directory, and it called the pre-per-record-units check(). Same lesson as the watchers.



# ---------------------------------------------------------------------------------------------
U["omega"] = "1/s"      # angular velocity is NOT dimensionless -- was "1", affecting 16 records
U["alpha"] = "1/s^2"    # angular acceleration, same error, 5 records

def check_record(r):
    """Dimensional check using THE RECORD'S OWN declared units, falling back to the global table
    only for symbols the record does not declare.

    The global table is single-valued and physics symbols are not. `base("R_1")` strips to `R`,
    which the table maps to *radius*, so every resistance record was ever checked against LENGTH.
    Measured: 32 distinct symbol collisions across 79 of 196 records -- 40% of the store was
    validated against the wrong unit. This is the identifier-collision class (IDENTIFIER_COLLISION.md):
    resolution must descend below the level at which the thing varies, and units vary PER RECORD.

    Does NOT verify: coefficients, signs, reciprocals, or omitted terms -- see
    RESULT_STORE_CLEANING.md. A dimensionally-valid wrong relation passes this cleanly."""
    f = r["f"]
    if "=" not in f: return ("skip", "no equation")
    own = r.get("units") or {}
    lhs, rhs = f.split("=", 1)
    lhs = lhs.strip()
    def unit_of(v):
        # EXACT MATCH ONLY. No base() fallback, no global-table guess.
        # base() strips Delta_p, Delta_t, Delta_S to one bucket carrying 11 different units, and
        # T_c (kelvin) to T (seconds). A VERIFIER THAT GUESSES CANNOT VERIFY: the guess was what
        # made this gate check resistance against length. Measured cost of removing it: 2 of 173
        # records become "unknown" -- and both were previously "checked" against a guessed unit.
        return own.get(v)
    vs = {v for v in SUB.findall(rhs) if v not in RESV}
    unknown = sorted({v for v in vs if unit_of(v) is None})
    if unknown: return ("unknown", ",".join(unknown[:4]))
    lu = unit_of(lhs)
    if lu is None: return ("unknown", lhs)
    sub = lambda x: SUB.sub(lambda m: f"(1 {unit_of(m.group(1))})" if m.group(1) not in RESV
                            else m.group(1), x)
    return ("call", f"<tool>conv<arg>{sub(rhs)}<arg>{lu}</tool>")


def audit_store(store, allow_unknown=False):
    """Driver. UNKNOWN COUNTS AS A FAILURE unless explicitly allowed.

    A check that skips on absent input cannot detect input that went absent. Three records had
    their units invalidated by an LHS repair; this gate returned "unknown" for each and they were
    counted separately from failures, so the gate covering that exact field reported nothing wrong
    for several turns. `allow_unknown=True` exists only for stores known to be mid-annotation."""
    import subprocess, re as _re
    bad = []
    for r in store:
        kind, payload = check_record(r)
        if kind == "unknown" and not allow_unknown:
            bad.append((r, f"UNKNOWN: {payload} -- cannot be checked, which is not the same as clean"))
        elif kind == "call":
            p = subprocess.run(["tools/eval/evalcli", payload], capture_output=True, text=True)
            m = _re.search(r"<res>(.*?)</res>", p.stdout, _re.S)
            if m and m.group(1).strip().startswith("!"):
                bad.append((r, f"DIMENSIONAL: {m.group(1).strip()}"))
    return bad

if __name__ == "__main__":
    import json as _j, sys as _s
    st = _j.load(open(_s.argv[1] if len(_s.argv) > 1 else "corpus/store_clean.json"))
    bad = audit_store(st)
    for r, why in bad: print(f"{r.get('rid','?')}  {r.get('f','?')[:40]:42} {why}")
    print(f"{len(bad)} finding(s) across {len(st)} records (unknown counts as a finding)")
    _s.exit(1 if bad else 0)
