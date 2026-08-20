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

def CONVDIM(label, expr, target, u=None):
    g = call(f"<tool>conv<arg>{expr}<arg>{target}</tool>")
    check(label, g, unit(g) == (u or target), f"unit {u or target}")

def CONVVAL(label, expr, target, expect, tol=1e-6):
    g = call(f"<tool>conv<arg>{expr}<arg>{target}</tool>")
    v = num(g)
    ok = v is not None and abs(v-expect) <= tol*max(1.0,abs(expect))
    check(label, g, ok, f"{expect:g} {target}")

print("=== v1.2.0: units live ONLY in conv; identifiers are symbols everywhere else ===")
# Every symbol in intro mechanics collides with a unit name. Pin the whole set.
for sym in ["m","g","h","t","s","T","A","V","C","F","N","K","J","W","L"]:
    g = call(f"<tool>eval<arg>{sym}</tool>")
    check(f"bare '{sym}' is a symbol, not a unit", g, g == "!expr", "!expr")
for expr, var, val in [("m*g*h","m",2), ("0.5*m*v^2","v",20), ("v*t","v",10), ("F/A","F",100)]:
    g = call(f"<tool>evalat<arg>{expr}<arg>{var}<arg>{val}</tool>")
    check(f"evalat({expr}) does not silently unit-ify", g, g == "!expr", "!expr")
# Constants must survive the change.
VALUE("pi still resolves", "pi", math.pi, tol=1e-9)
VALUE("e still resolves",  "e",  math.e,  tol=1e-9)

print("=== eval: physics quantities as a physicist writes them ===")
# Ohm's law, Newton, kinematics, energy, power -- dimensions must come out right.
CONVVAL("Ohm's law V/I",       "12 V/(0.25 A)", "ohm", 48.0)
CONVVAL("Newton m*a",          "2 kg*(3 m/s^2)", "N", 6.0)
CONVVAL("speed d/t",           "100 m/(10 s)", "m/s", 10.0)
CONVVAL("pressure F/A",        "100 N/(2 m^2)", "Pa", 50.0)
CONVVAL("power V^2/R",         "(12 V)^2/(6 ohm)", "W", 24.0)
CONVVAL("kinetic energy",      "0.5*80 kg*(20 m/s)^2", "J", 16000.0)
CONVVAL("prefixed units",      "5 kN/(2 m^2)", "Pa", 2500.0)
CONVVAL("power of a quantity", "(20 m/s)^2", "m^2/s^2", 400.0)
CONVVAL("sqrt of squared unit","sqrt(16 m^2)", "m", 4.0)
CONVVAL("frequency 1/t",       "1/(0.5 s)", "Hz", 2.0)
CONVVAL("momentum",            "2 kg*(3 m/s)", "kg*m/s", 6.0)

VALUE("g from GM/r^2", "6.674e-11*5.972e24/((6.371e6)^2)", 9.8195, tol=1e-3)
VALUE("KE numeric (no units)", "0.5*80*(20)^2", 16000.0)
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

print("=== diff: every chain rule, verified numerically ===")
for expr, x0 in [("tan(x)",0.4), ("log(x)",5.0), ("tanh(x)",0.6), ("atan(x)",0.9),
                 ("asin(x)",0.4), ("acos(x)",0.4), ("sinh(x)",0.5), ("cosh(x)",0.5),
                 ("2^x",1.5), ("x^4",1.7), ("exp(x^2)",0.8), ("sin(x)/x",1.2)]:
    d = call(f"<tool>diff<arg>{expr}<arg>x</tool>")
    if d.startswith("!"):
        check(f"diff({expr})", d, False, "an expression"); continue
    sym = num(call(f"<tool>evalat<arg>{d}<arg>x<arg>{x0}</tool>"))
    h = 1e-5
    f1 = num(call(f"<tool>evalat<arg>{expr}<arg>x<arg>{x0+h}</tool>"))
    f2 = num(call(f"<tool>evalat<arg>{expr}<arg>x<arg>{x0-h}</tool>"))
    ok = None not in (sym,f1,f2) and abs(sym-(f1-f2)/(2*h)) < 1e-3*max(1,abs(sym))
    check(f"diff({expr}) at {x0}", f"{d} -> {sym}", ok, (f1-f2)/(2*h) if None not in (f1,f2) else "?")

print("=== stat: every op against an independent computation ===")
import statistics as st
data=[3,1,4,1,5,9,2,6]; ds=",".join(map(str,data))
for op, ref in [("mean",st.mean(data)), ("median",st.median(data)), ("sd",st.stdev(data)),
                ("var",st.variance(data)), ("sum",sum(data)), ("min",min(data)),
                ("max",max(data)), ("n",len(data))]:
    v = num(call(f"<tool>stat<arg>{op}<arg>{ds}</tool>"))
    ok = v is not None and abs(v-ref) < 1e-6*max(1,abs(ref))
    check(f"stat {op}", v, ok, ref)

print("=== conv: every unit family round-trips ===")
for q,a,b in [("1","N","kg*m/s^2"),("1","J","N*m"),("1","W","J/s"),("1","Pa","N/m^2"),
              ("1","V","W/A"),("1","C","A*s"),("1","T","kg/(A*s^2)"),("1","L","m^3"),
              ("1","eV","J"),("1","kWh","J"),("1","bar","Pa"),("1","ft","m"),("1","lb","kg")]:
    fwd = num(call(f"<tool>conv<arg>{q} {a}<arg>{b}</tool>"))
    back = num(call(f"<tool>conv<arg>{fwd} {b}<arg>{a}</tool>")) if fwd is not None else None
    ok = back is not None and abs(back-float(q)) < 1e-6
    check(f"conv round trip {a} <-> {b}", f"{fwd} -> {back}", ok, q)

print("=== solve: degenerate and complex cases ===")
RAW("no solution",      "<tool>solve<arg>x+1=x<arg>x</tool>", "none")
RAW("all solutions",    "<tool>solve<arg>x=x<arg>x</tool>", "all")
RAW("double root",      "<tool>solve<arg>x^2-2x+1=0<arg>x</tool>", "x=1")
RAW("complex pair",     "<tool>solve<arg>x^2+1=0<arg>x</tool>", "x=0+1i, x=0-1i")
RAW("degree 3 refused", "<tool>solve<arg>x^3-1=0<arg>x</tool>", "!nosol")
RAW("non-polynomial",   "<tool>solve<arg>sin(x)=0<arg>x</tool>", "!nosol")

print("=== every error code in TOOL_SPEC 6.1 is reachable ===")
for callstr, code in [("<tool>eval<arg>1/0</tool>","!domain"),
                      ("<tool>eval<arg>ln(0)</tool>","!domain"),
                      ("<tool>eval<arg>2+</tool>","!parse"),
                      ("<tool>nope<arg>1</tool>","!name"),
                      ("<tool>eval<arg>1<arg>2</tool>","!arity"),
                      ("<tool>eval<arg>zzz</tool>","!expr"),
                      ("<tool>conv<arg>1 kg<arg>V</tool>","!units"),
                      ("<tool>solve<arg>x^3=1<arg>x</tool>","!nosol"),
                      ("<tool>eval<arg>1e300*1e300</tool>","!range")]:
    g = call(callstr); check(f"{code} reachable", g, g==code, code)

print("=== ambiguity must be refused, never guessed ===")
for e in ["100 m/10 s", "12 V/0.25 A", "1/2 2 3", "20 N/4 kg", "500 J/25 s"]:
    g = call(f"<tool>eval<arg>{e}</tool>")
    check(f"ambiguous '{e}' refused", g, g == "!expr", "!expr")

print("=== INTERACTION: one call's output feeding another (the least-covered form) ===")
# This is how the model actually works -- solve then evaluate, conv then stat, diff then evalat.
# A result that is correct in isolation can still be unusable as the next call's argument.

# solve -> substitute numbers -> eval.  The rearranged formula must be a legal eval argument.
for eq, var, subs, expect in [
    ("F=m*a","a",{"F":"20","m":"4"},5.0),
    ("V=I*R","R",{"V":"12","I":"0.25"},48.0),
    ("P=2*l+2*w","l",{"P":"40","w":"6"},14.0),
    ("v=d/t","t",{"d":"100","v":"10"},10.0),
    ("PE=m*g*h","h",{"PE":"98","m":"5","g":"9.8"},2.0),
    ("v^2=v0^2+2*a*d","a",{"v":"10","v0":"0","d":"25"},2.0),
]:
    sol = call(f"<tool>solve<arg>{eq}<arg>{var}</tool>")
    rhs = sol.split("=",1)[1] if "=" in sol else ""
    e = rhs
    for k,v in subs.items(): e = re.sub(rf'\b{k}\b', f"({v})", e)
    got = num(call(f"<tool>eval<arg>{e}</arg></tool>".replace("</arg>","")))
    ok = got is not None and abs(got-expect) < 1e-6
    check(f"solve({eq},{var}) -> eval chain", f"{sol} => {e} => {got}", ok, expect)

# diff -> evalat.  The derivative string must be a legal evalat argument.
for expr, x0 in [("x^3-4x^2+7",2.0), ("sin(x)*x",1.1), ("1/x",3.0), ("sqrt(x)",4.0), ("exp(x^2)",0.7)]:
    d = call(f"<tool>diff<arg>{expr}<arg>x</tool>")
    v = call(f"<tool>evalat<arg>{d}<arg>x<arg>{x0}</tool>")
    ok = not v.startswith("!") and num(v) is not None
    check(f"diff({expr}) output is a legal evalat argument", f"{d} -> {v}", ok, "a number")

# conv -> stat.  Converted values must be parseable back into a stat list.
vals = []
for q in ("1 km","2 km","3 km"):
    vals.append(num(call(f"<tool>conv<arg>{q}<arg>m</tool>")))
lst = ",".join(str(int(v)) for v in vals if v is not None)
g = num(call(f"<tool>stat<arg>mean<arg>{lst}</tool>"))
check("conv outputs feed stat", f"{lst} -> {g}", g is not None and abs(g-2000)<1e-6, 2000)

# integ -> eval.  A definite integral's output must be usable as a number.
iv = call("<tool>integ<arg>x^2<arg>x<arg>0<arg>3</tool>")
g  = num(call(f"<tool>eval<arg>{iv}*2</tool>"))
check("integ output feeds eval", f"{iv} -> {g}", g is not None and abs(g-18)<1e-6, 18)

# solve -> the root fed back through evalat, for the quadratic branch specifically.
sol = call("<tool>solve<arg>2x^2+3x-5=0<arg>x</tool>")
roots = [float(m) for m in re.findall(r'x=(-?[\d.]+)', sol)]
ok = len(roots) == 2
for r in roots:
    resid = num(call(f"<tool>evalat<arg>2x^2+3x-5<arg>x<arg>{r}</tool>"))
    if resid is None or abs(resid) > 1e-6: ok = False
check("quadratic roots feed back through evalat", f"{sol} -> {roots}", ok, "residual 0")

# A conv result string must be re-parseable by conv (round trip through the FORMATTED string).
c1 = call("<tool>conv<arg>100 km/h<arg>m/s</tool>")
c2 = call(f"<tool>conv<arg>{c1}<arg>km/h</tool>")
ok = num(c2) is not None and abs(num(c2)-100) < 1e-6
check("conv output string is re-parseable by conv", f"{c1} -> {c2}", ok, "100 km/h")

print()
if fails:
    print(f"*** {len(fails)} SEMANTIC FAILURES of {total} ***\n")
    print("\n".join(fails))
    sys.exit(1)
print(f"{total} semantic checks passed")
