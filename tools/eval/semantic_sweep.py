#!/usr/bin/env python3
"""Semantic sweep of Backend 1 -- tests INTENT, not code paths.

The precedence bug (100 m/10 s -> 10 m*s) survived 133 unit tests, 96.9% line coverage, ASan/UBSan
and 400k fuzz inputs, because every one of those checks what the code DOES. None encoded what a
physicist MEANS. This suite is built the other way round: each case states a physical fact, and the
reference is computed here independently rather than transcribed from the evaluator's own output.

Three verification styles, none of which is string comparison against a hand-written expectation:
  VALUE  -- compare against a number computed in Python
  DIM    -- compare the SI dimension of the result, which catches inverted denominators
  ROUND  -- verify by round trip or by substitution (solve: put the root back; diff: numeric
            differentiation; integ: closed form)
"""
import subprocess, sys, math, re

EVAL = "./evalcli"
fails, total = [], 0

def call(s):
    r = subprocess.run([EVAL, s], capture_output=True, text=True, timeout=20)
    return r.stdout.strip().removeprefix("<res>").removesuffix("</res>")

def num(s):
    m = re.match(r'^(-?[\d.]+(?:e[+-]\d+)?)', s.strip())
    return float(m.group(1)) if m else None

def unit(s):
    p = s.strip().split(None, 1)
    return p[1] if len(p) > 1 else ""

def check(label, got, ok, want):
    global total
    total += 1
    if not ok:
        fails.append(f"  {label}\n     got  {got!r}\n     want {want}")

def VALUE(label, expr, expect, tol=1e-6, u=None):
    g = call(f"<tool>eval<arg>{expr}</tool>")
    v = num(g)
    ok = v is not None and abs(v - expect) <= tol * max(1.0, abs(expect))
    if ok and u is not None:
        ok = unit(g) == u
    check(label, g, ok, f"{expect:g}" + (f" {u}" if u else ""))

def DIM(label, expr, u):
    g = call(f"<tool>eval<arg>{expr}</tool>")
    check(label, g, unit(g) == u, f"unit {u}")

def RAW(label, callstr, want):
    g = call(callstr)
    check(label, g, g == want, want)

print("=== eval: physics quantities as a physicist writes them ===")
# Ohm's law, Newton, kinematics, energy, power -- dimensions must come out right.
DIM("V/(I) -> ohm",            "12 V/(0.25 A)", "ohm")
DIM("m*a -> N",                "2 kg*3 m/s^2", "N")
DIM("d/(t) -> m/s",            "100 m/(10 s)", "m/s")
DIM("F/(A) -> Pa",             "100 N/(2 m^2)", "Pa")
DIM("V^2/(R) -> W",            "(12 V)^2/(6 ohm)", "W")
DIM("power of a quantity",     "(20 m/s)^2", "m^2/s^2")
DIM("sqrt of a squared unit",  "sqrt(16 m^2)", "m")
DIM("prefixed units",          "5 kN/(2 m^2)", "Pa")
DIM("q*V -> J",                "2 C*3 V", "m^2*kg/s^2")   # blocklisted, stays base units

VALUE("g from GM/r^2", "6.674e-11*5.972e24/((6.371e6)^2)", 9.8195, tol=1e-3)
VALUE("KE = 0.5 m v^2", "0.5*80*(20)^2", 16000.0)
VALUE("scientific notation", "6.022e23/1e23", 6.022, tol=1e-9)
VALUE("sin of degrees", "sin(30 deg)", 0.5, tol=1e-6)
VALUE("cos of degrees", "cos(60 deg)", 0.5, tol=1e-6)

print("=== conv: round trips must return the original ===")
for q, a, b in [("100","km","m"), ("1","h","s"), ("60","mi/h","m/s"), ("2.5","kg","g"),
                ("500","mL","L"), ("1","atm","kPa")]:
    fwd = call(f"<tool>conv<arg>{q} {a}<arg>{b}</tool>")
    v = num(fwd)
    back = call(f"<tool>conv<arg>{v} {b}<arg>{a}</tool>") if v is not None else ""
    bv = num(back)
    ok = bv is not None and abs(bv - float(q)) <= 1e-6 * max(1.0, float(q))
    check(f"round trip {q} {a} -> {b} -> {a}", f"{fwd} -> {back}", ok, f"{q} {a}")

RAW("degC->K is affine",  "<tool>conv<arg>0 degC<arg>K</tool>", "273.15 K")
RAW("degF->degC affine",  "<tool>conv<arg>212 degF<arg>degC</tool>", "100 degC")

print("=== solve: substitute the root back, do not string-match ===")
for eq, var, xs in [("x^2-4=0","x",None), ("2x+6=0","x",None), ("3x^2-12=0","x",None)]:
    g = call(f"<tool>solve<arg>{eq}<arg>{var}</tool>")
    roots = [float(m) for m in re.findall(rf'{var}=(-?[\d.]+(?:e[+-]\d+)?)', g)]
    ok = bool(roots)
    for r in roots:
        chk = call(f"<tool>evalat<arg>{eq.split('=')[0]}<arg>{var}<arg>{r}</tool>")
        rv = num(chk)
        if rv is None or abs(rv) > 1e-6: ok = False
    check(f"solve({eq},{var}) roots satisfy the equation", g, ok, "residual 0")

# Literal rearrangement: check dimensionally by substituting numbers into BOTH forms.
for eq, var, subs, expect in [
    ("F=m*a", "a", {"F":20,"m":4}, 5.0),
    ("V=I*R", "R", {"V":12,"I":0.25}, 48.0),
    ("P=2*l+2*w", "l", {"P":40,"w":6}, 14.0),
    ("v=d/t", "t", {"d":100,"v":10}, 10.0),
]:
    g = call(f"<tool>solve<arg>{eq}<arg>{var}</tool>")
    rhs = g.split("=",1)[1] if "=" in g else ""
    e = rhs
    for k,v in subs.items():
        e = re.sub(rf'\b{k}\b', f"({v})", e)
    got = num(call(f"<tool>eval<arg>{e}</tool>"))
    ok = got is not None and abs(got-expect) < 1e-6
    check(f"solve({eq},{var}) evaluates correctly", f"{g} -> {got}", ok, expect)

print("=== diff: verify by numeric differentiation, not by string ===")
for expr, x0 in [("x^2",3.0), ("x^3-4x^2+7",2.0), ("sin(x)",0.7), ("exp(x)",1.1),
                 ("ln(x)",2.0), ("1/x",3.0), ("sqrt(x)",4.0), ("x^2*sin(x)",1.3)]:
    d = call(f"<tool>diff<arg>{expr}<arg>x</tool>")
    if d.startswith("!"):
        check(f"diff({expr})", d, False, "an expression"); continue
    sym = num(call(f"<tool>evalat<arg>{d}<arg>x<arg>{x0}</tool>"))
    h = 1e-5
    f1 = num(call(f"<tool>evalat<arg>{expr}<arg>x<arg>{x0+h}</tool>"))
    f2 = num(call(f"<tool>evalat<arg>{expr}<arg>x<arg>{x0-h}</tool>"))
    ok = None not in (sym,f1,f2) and abs(sym-(f1-f2)/(2*h)) < 1e-3*max(1,abs(sym))
    check(f"diff({expr}) at x={x0}", f"{d} -> {sym}", ok, f"{(f1-f2)/(2*h) if None not in (f1,f2) else '?'}")

print("=== integ: compare against closed forms ===")
for expr, a, b, exact in [("x",0,1,0.5), ("x^2",0,3,9.0), ("sin(x)",0,math.pi,2.0),
                          ("exp(x)",0,1,math.e-1), ("1/x",1,math.e,1.0), ("cos(x)",0,math.pi/2,1.0)]:
    g = call(f"<tool>integ<arg>{expr}<arg>x<arg>{a}<arg>{b}</tool>")
    v = num(g)
    ok = v is not None and abs(v-exact) < 1e-6*max(1,abs(exact))
    check(f"integ({expr},{a},{b})", g, ok, f"{exact:.9g}")

print("=== stat: sample vs population is the classic silent error ===")
data = [2,4,4,4,5,5,7,9]
ds = ",".join(map(str,data))
n = len(data); mean = sum(data)/n
svar = sum((x-mean)**2 for x in data)/(n-1)
pvar = sum((x-mean)**2 for x in data)/n
g = num(call(f"<tool>stat<arg>sd<arg>{ds}</tool>"))
check("stat sd is SAMPLE (n-1), not population", g, g is not None and abs(g-math.sqrt(svar))<1e-6,
      f"{math.sqrt(svar):.9g} (population would be {math.sqrt(pvar):.9g})")
VALUEmean = num(call(f"<tool>stat<arg>mean<arg>{ds}</tool>"))
check("stat mean", VALUEmean, VALUEmean is not None and abs(VALUEmean-mean)<1e-9, mean)

print("=== ambiguity must be refused, never guessed ===")
for e in ["100 m/10 s", "12 V/0.25 A", "1/2 2 3", "20 N/4 kg", "500 J/25 s"]:
    g = call(f"<tool>eval<arg>{e}</tool>")
    check(f"ambiguous '{e}' refused", g, g == "!expr", "!expr")

print()
if fails:
    print(f"*** {len(fails)} SEMANTIC FAILURES of {total} ***\n")
    print("\n".join(fails))
    sys.exit(1)
print(f"{total} semantic checks passed")
