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

if __name__ == "__main__":
    recs = json.load(open(sys.argv[1] if len(sys.argv)>1 else "../../corpus/records_raw.json"))
    calls, idx, tally = [], [], collections.Counter()
    for r in recs:
        kind, payload = check(r["f"])
        tally[kind] += 1
        if kind == "call": calls.append(payload); idx.append(r)
        else: r["dim"] = kind
    out = re.findall(r"<res>(.*?)</res>", subprocess.run(["./evalcli","-"],
          input="\n".join(calls)+"\n", capture_output=True, text=True).stdout, re.S)
    bad = []
    for r, o in zip(idx, out):
        r["dim"] = "fail" if o.startswith("!") else "ok"
        if o.startswith("!"): bad.append((r["f"], r["name"], o))
    ok = sum(1 for r in recs if r.get("dim")=="ok")
    print(f"records checked        {len(recs)}")
    print(f"  symbols not in table {tally['unknown']:>4}   (NOT a physics failure -- unjudged)")
    print(f"  dimensionally OK     {ok:>4}")
    print(f"  DIMENSIONALLY WRONG  {len(bad):>4}   <- would have shipped")
    print(f"\n  first 12 rejected, of {len(bad)}:")
    for f,n,o in bad[:12]: print(f"    {o:<8} {f[:44]:<46} {n[:38]}")
    json.dump(recs, open("../../corpus/records_raw.json","w"), indent=1)

    # the gate must fire on its own mutants
    print("\n  mutation check on this gate:")
    for mut, why in [("v=d+t","distance plus time"),("F=m/a","second law inverted"),
                     ("K=0.5*m*v","missing the square"),("v=d/t","CORRECT -- must NOT fire")]:
        kind, payload = check(mut)
        res = run_ = subprocess.run(["./evalcli", payload], capture_output=True, text=True).stdout.strip() if kind=="call" else kind
        fired = res.startswith("<res>!")
        want = "must NOT" not in why
        print(f"    {'ok ' if fired==want else '** '}{mut:<14}{res:<22}{why}")
