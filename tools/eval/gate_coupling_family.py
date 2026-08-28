#!/usr/bin/env python3
"""PERMANENT GATE: a two-factor product record must bound the product, or say why it need not.

WHY. The n=600 read found that FIVE of nine root causes were direct family siblings of records the
A32 coupling pass had fixed. The bounds are keyed by exact formula string, so:

    v_t = r*omega   -> rim speed bounded at 1 km/s        (fixed)
    a_t = r*alpha   -> nothing                            (missed)

and the `r` window on the missed record is BYTE-IDENTICAL to the fixed one, same justification
string. The fixes were correct, measured, and applied per-record against a per-FAMILY defect. An
audit that asks "does this record have a bound?" answers no and moves on; nothing asked "does this
record look like one that needed a bound?"

THE PROPERTY, not a proxy for it: a record whose right-hand side is a bare product or quotient of
exactly two free variables computes a quantity that is the PRODUCT of two independently drawn
windows. Neither window constrains it. Such a record must carry either a _PRECONDITION (coupling
the draws) or a non-None _RESULT_KIND (bounding the result), or be listed here as reviewed and
genuinely unbounded -- with the reason, because "no physics ties A to B" has been wrong before:
it was true of r and omega separately and false of their product.
"""
import importlib.util, io, contextlib, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

# Reviewed and genuinely unbounded: the product spans real physics with no material or lawful cap.
EXEMPT = {
    # --- the product is a DEFINITION of an unbounded extensive quantity ---------------------------
    "W=F*d":                 "work: a force times a distance is any energy",
    "W_i=F_i*d_i":           "work, input side of a machine",
    "W_o=F_o*d_o":           "work, output side of a machine",
    "tau=r*F":               "torque: unbounded for the same reason as work",
    "E=P*t":                 "energy delivered over a time is unbounded",
    "P=((W)/(t))":           "power is work over time; both windows are already declared",
    "P=F*v":                 "mechanical power, unbounded",
    "P=tau*omega":           "rotational power, unbounded",
    "P=V*I":                 "electrical power, unbounded",
    "P_ave=I_rms*V_rms":     "electrical power, unbounded",
    "Delta_p=F_net*Delta_t": "impulse is unbounded",
    "Delta_p=m*Delta_v":     "momentum change is unbounded",
    "p=m*v":                 "momentum: bounded only through v, which sub_light covers",
    "I=((P)/(A))":           "intensity: a power spread over an area, unbounded",
    "p=((F)/(A))":           "pressure: a force over an area, unbounded",
    "Delta_S=((Q)/(T))":     "entropy change: unbounded",
    "V=((U_E)/(q))":         "potential: unbounded",
    "C=((Q)/(V))":           "capacitance: unbounded",
    # --- NEWTON'S SECOND LAW AND ITS REARRANGEMENTS ----------------------------------------------
    # A force is any force and a mass is any mass. There is no product bound here; both factors are
    # separately declared and their product spans the physics honestly.
    "F=m*a":                 "second law: force is unbounded",
    "F_net=m*a":             "second law",
    "F_c=m*a_c":             "second law, centripetal",
    "a=((F)/(m))":           "second law rearranged",
    "m=F_net/a":             "second law rearranged",
    "F_net=((Delta_p)/(Delta_t))": "second law in impulse form",
    # --- RATIOS WHOSE RESULT IS ALREADY BOUNDED OR WHOSE FACTORS ARE ------------------------------
    "rho=((m)/(V))":         "result-bounded by material_density",
    "v=d/t":                 "result-bounded by sub_light",
    "x=v_0x*t":              "a displacement; v_0x is sub_light-bounded and t is declared",
    "omega=v/r":             "result-bounded: v is sub_light and r is a declared length",
    "omega=((theta)/(t))":   "angular speed from a declared angle and a declared time",
    "omega=((Delta_theta)/(Delta_t))": "angular speed, same",
    "alpha=((Delta_omega)/(Delta_t))": "angular acceleration from two declared windows",
    "alpha=((a_t)/(r))":     "the INVERSE of a_t=r*alpha, which now carries the shear precondition",
    "a=((Delta_v)/(Delta_t))": "acceleration from two declared windows",
    "s=r*theta":             "arc length: a declared radius times a declared angle is a real length",
    "d_CM=R*theta":          "rolling distance, same as arc length",
    "m=rho*V":               "mass from a material density and a volume, both declared",
    # --- ELECTRICAL, where the spans are genuinely 12 orders wide ---------------------------------
    "R=V/I":                 "resistance spans 12 orders in real circuits",
    "V=I*R":                 "voltage likewise",
    "I_0=((V_0)/(Z))":       "AC current from a declared voltage and impedance",
    "I_rms=V_rms/Z":         "AC rms current, same",
    "tau=R*C":               "an RC time constant: both factors declared, product spans real circuits",
}

TWO_FACTOR = re.compile(r"^\(*([A-Za-z][A-Za-z0-9_]*)\)*\s*([*/])\s*\(*([A-Za-z][A-Za-z0-9_]*)\)*$")


def main():
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(m)

    candidates, missing = [], []
    for r in m.recs:
        f = r["f"]
        rhs = f.split("=", 1)[1]
        free = [v for v in sorted({x for x in m.VAR.findall(rhs)} - {"pi", "e"})
                if m._const_for(r, v) is None]
        if len(free) != 2:
            continue
        if not TWO_FACTOR.match(rhs.replace(" ", "")):
            continue
        candidates.append(f)
        if f in EXEMPT:
            continue
        if f in m._PRECONDITION:
            continue
        if m._RESULT_KIND.get(f) is not None:
            continue
        missing.append((f, free))

    print(f"  two-factor product records: {len(candidates)}   "
          f"exempt-with-reason: {sum(1 for c in candidates if c in EXEMPT)}")
    for f, free in missing:
        print(f"  UNBOUNDED PRODUCT  {f}   free={free}")
        print(f"                     neither a _PRECONDITION nor a _RESULT_KIND bounds {free[0]}*{free[1]}")
    if missing:
        print(f"\n  FAIL: {len(missing)} record(s) compute a product of two independently drawn")
        print("  windows with nothing bounding the product. Add a precondition, a result bound, or")
        print("  an EXEMPT entry stating why the product is genuinely unbounded.")
        return 1
    if not candidates:
        print("  CANNOT CHECK: no two-factor records matched. Refusing to report a pass.")
        return 2
    print("  PASS: every two-factor product record is bounded or exempt with a stated reason")
    return 0


if __name__ == "__main__":
    sys.exit(main())
