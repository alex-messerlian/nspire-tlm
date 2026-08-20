#!/usr/bin/env python3
"""Dimensionless-but-numerically-significant audit.

Three bugs of this shape have now landed (sin(30 deg), the unit-name collision, rev-in-a-rate).
This enumerates every quantity the system can represent whose dimension vector is empty (or whose
arithmetic is otherwise non-distributive) and states the REQUIRED behaviour for each before running
it. Anything where observed != required is a finding, not a curiosity."""
import subprocess, re, sys
def run(cs):
    p = subprocess.run(["./evalcli","-"], input="\n".join(cs)+"\n", capture_output=True, text=True)
    return re.findall(r"<res>(.*?)</res>", p.stdout, re.S)

# (class, call, required, why)
P = [
 # --- C1 angle constants: scale != 1, dimension empty --------------------------
 ("C1 angle","<tool>eval<arg>sin(30 deg)</tool>","0.5","deg folds to rad before sin"),
 ("C1 angle","<tool>eval<arg>sin(30)</tool>","-0.9880316241","bare number is RADIANS, not degrees"),
 ("C1 angle","<tool>conv<arg>(5 rev)/(2 s)<arg>Hz</tool>","!units","invariant 6: angle constant in a rate"),
 ("C1 angle","<tool>conv<arg>sin(30 deg)/(2 s)<arg>Hz</tool>","0.25 Hz","sin CONSUMED the angle -- legitimate Hz, and the case a syntactic lint would wrongly refuse"),
 ("C1 angle","<tool>conv<arg>5/(2 s)<arg>Hz</tool>","2.5 Hz","no angle anywhere"),
 ("C1 angle","<tool>conv<arg>(5 rev)/(2 s)<arg>rad/s</tool>","15.70796327 rad/s","legitimate: rev IS 2pi rad"),
 ("C1 angle","<tool>eval<arg>(30 deg)^2</tool>","0.2741556778","LEGAL: angle^2 appears in small-angle expansions"),
 ("C1 angle","<tool>eval<arg>1+30 deg</tool>","1.523598776","LEGAL: angles are dimensionless, series expansions need this"),
 ("C1 angle","<tool>conv<arg>asin(0.5)<arg>deg</arg></tool>","!units","malformed target text"),
 ("C1 angle","<tool>conv<arg>asin(0.5)<arg>deg</tool>","30 deg","inverse trig returns rad; naming it deg"),
 ("C1 angle","<tool>eval<arg>cos(0.25 rev)</tool>","6.123233996e-17","FLAGGED: fp noise, a section 5.1 formatting question -- not a units bug"),
 ("C1 angle","<tool>eval<arg>tan(45 deg)</tool>","1","canonical"),
 # --- C2 affine units: offset means arithmetic does not distribute -------------
 ("C2 affine","<tool>conv<arg>2*(10 degC)<arg>degC</tool>","!units","doubling an absolute temperature is meaningless"),
 ("C2 affine","<tool>conv<arg>(20 degC)+(5 degC)<arg>degC</tool>","!units","adding two absolute temperatures"),
 ("C2 affine","<tool>conv<arg>(20 degC)-(5 degC)<arg>K</tool>","15 K","a DIFFERENCE of temps is legitimate"),
 ("C2 affine","<tool>conv<arg>(212 degF)-(32 degF)<arg>K</tool>","100 K","180 degF interval is 100 K"),
 ("C2 affine","<tool>conv<arg>(50 degC)-(20 degF)<arg>K</tool>","!units","mixed affine units: refuse, do not guess"),
 ("C2 affine","<tool>conv<arg>-40 degC<arg>degF</tool>","-40 degF","the crossover; also checks unary minus vs subtraction"),
 ("C2 affine","<tool>conv<arg>100 degC<arg>degF</tool>","212 degF","canonical offset conversion"),
 ("C2 affine","<tool>conv<arg>(2 kg)*(10 degC)<arg>kg*K</tool>","!units","offset unit in a product"),
 # --- C3 log-scale pseudo-units: not linear, must never be a unit --------------
 ("C3 log","<tool>eval<arg>20 dB</tool>","!expr","dB is not a unit and must not parse as one"),
 ("C3 log","<tool>eval<arg>7 pH</tool>","!expr","pH likewise"),
 ("C3 log","<tool>eval<arg>10*log(1000)</tool>","30","the correct way to express dB"),
 ("C3 log","<tool>conv<arg>ln(5 m)<arg>1</tool>","!units","log of a dimensioned quantity"),
 ("C3 log","<tool>conv<arg>exp(2 s)<arg>1</tool>","!units","exp of a dimensioned quantity"),
 ("C3 log","<tool>conv<arg>2^(3 m)<arg>1</tool>","!units","dimensioned exponent"),
 # --- C4 true ratios: dimension genuinely cancels, value is meaningful ---------
 ("C4 ratio","<tool>conv<arg>(50 J)/(200 J)<arg>1</arg></tool>","!units","stray </arg> makes the target the text \"1</arg>\", which is not a unit; arity is still 2 so !units is right"),
 ("C4 ratio","<tool>conv<arg>(50 J)/(200 J)<arg>1</tool>","0.25 1","efficiency -- FLAGGED: trailing \"1\" is a section 5.1 formatting wart"),
 ("C4 ratio","<tool>conv<arg>(0.002 m)/(1 m)<arg>m/m</tool>","0.002 m/m","strain"),
 ("C4 ratio","<tool>conv<arg>(3e8 m/s)/(2e8 m/s)<arg>1</tool>","1.5 1","refractive index"),
 ("C4 ratio","<tool>conv<arg>(1000 kg/m^3)*(2 m/s)*(0.05 m)/(0.001 Pa*s)<arg>1</tool>","100000 1","Reynolds number"),
 ("C4 ratio","<tool>conv<arg>(340 m/s)/(170 m/s)<arg>1</tool>","2 1","Mach number"),
 # --- C5 percent / parts-per: scale != 1, dimension empty ---------------------
 ("C5 percent","<tool>eval<arg>50 %</tool>","!parse","% must not parse as a unit"),
 ("C5 percent","<tool>eval<arg>0.85*100</tool>","85","percent is the generator's job, not the evaluator's"),
 ("C5 percent","<tool>eval<arg>5 ppm</tool>","!expr","ppm likewise"),
 # --- C6 counts: dimensionless AND scale 1, but must not fold to an angle -----
 ("C6 count","<tool>conv<arg>12/(4 s)<arg>1/s</tool>","3 1/s","a bare count over time"),
 ("C6 count","<tool>conv<arg>12/(4 s)<arg>Hz</tool>","3 Hz","count/time named as frequency"),
 # --- C7 solid angle / steradian ---------------------------------------------
 ("C7 solid","<tool>eval<arg>4*pi sr</tool>","!expr","sr is not defined and must not silently vanish"),
]
res = run([c for _,c,_,_ in P])
find = []
print(f"{'class':<12}{'call':<52}{'got':<22}{'required':<22}")
for (cls, call, req, why), got in zip(P, res):
    ok = (got == req)
    if not ok: find.append((cls, call, got, req, why))
    body = call[len("<tool>"):-len("</tool>")].replace("<arg>","(",1).replace("<arg>",",")
    print(f"{'  ok ' if ok else '  ** '}{cls:<10}{body[:50]:<52}{got[:20]:<22}{req[:20]:<22}")
print(f"\nprobes {len(P)}   findings {len(find)}")
for f in find: print(f"\n  FINDING [{f[0]}] {f[1]}\n    got={f[2]!r} required={f[3]!r}\n    {f[4]}")
sys.exit(1 if find else 0)
