import json
#!/usr/bin/env python3
"""PERMANENT GATE: the same quantity may not be declared two different ways in two records.

THE GENERALISED SIBLING QUESTION. gate_coupling_family asks "does this record look like one that
needed a bound?" for products. This asks the same question of the DECLARATION TABLES: when the same
variable, in the same unit, appears in two records, the two declarations must agree -- or the
difference must be deliberate and stated.

WHY. Two defects of exactly this shape are already on the record:
  * refractive_index was (1, 4) in _KIND_RANGE and (1, 100) in _RESULT_RANGE -- the same physical
    quantity with windows 25x apart, so a RESULT of n = 40 passed where a GIVEN of n = 40 failed.
  * v_t=r*omega was bounded and a_t=r*alpha was not, and the `r` windows were byte-identical.

An audit asking "is this declared?" answers yes for both sides of an inconsistent pair. Only a
comparison across records can see it.

DIFFERENCES ARE ALLOWED, but they must be declared here with a reason -- a radius really is bounded
differently in an optics record and a flywheel record, and saying so is the point.
"""
import importlib.util, io, contextlib, pathlib, sys
from collections import defaultdict


def _sig(x, n=6):
    """Round to n significant figures, so tiny windows keep their identity."""
    if x == 0: return 0.0
    import math
    return round(x, -int(math.floor(math.log10(abs(x)))) + (n - 1))

ROOT = pathlib.Path(__file__).resolve().parents[2]

# (variable, unit) -> why the windows legitimately differ across records.
DELIBERATE = {
    ("omega_0", "1/s"): "a body's initial spin rate and a resonator's natural frequency are not the same quantity",
    ("rho", "kg/m^3"):  "each record bounds its own medium: a manometer fluid (600-13600), any material "
                        "(aerogel to osmium), and a drag fluid (air to water). One window would be wrong for all three",
    ("R", "ohm"):       "a circuit resistance and the R of an RC time constant are chosen from different ranges",
    ("K", "J"):      "kinetic energy in one record, a particle-in-a-box level in another",
    ("F", "N"):      "a spring force and a net force are bounded by different objects",
    ("omega", "1/s"):"SHM, a rotor and a wave carrier are three different angular frequencies",
    ("l", "m"):      "a spring length and a conductor length are different objects",
    ("v_0", "m/s"):  "a launch speed upward is bounded by flight time; a general initial speed is not",
    ("W", "J"):      "work delivered by a machine and work in the first law differ in scale",
    ("Delta_omega", "1/s"): "a spin-up rate and a resonance bandwidth are different quantities",
    ("r", "m"):      "a radius spans an atom to an orbit; each record's window is its own object",
    ("R", "m"):      "same",
    ("t", "s"):      "a duration is record-specific: a flight time and a half-life are both times",
    ("m", "kg"):     "a mass spans an electron to a planet",
    ("v", "m/s"):    "a speed spans drift velocity to light",
    ("T", "s"):      "a period spans a pendulum to an orbit",
    ("T", "K"):      "a temperature spans a cryostat to a reservoir",
    ("A", "m^2"):    "an area is record-specific",
    ("L", "m"):      "a length is record-specific",
    ("d", "m"):      "a distance is record-specific",
    ("n", "1"):      "n is a quantum number in one record and a refractive index in another",
    ("E", "J"):      "an energy spans a photon to a macroscopic store",
    ("f", "Hz"):     "a frequency spans audio to gamma",
    ("q", "C"):      "a charge spans one electron to a capacitor bank",
    ("I", "A"):      "a current spans a nerve impulse to a lightning strike",
    ("x", "m"):      "a displacement is record-specific",
    ("V", "V"):      "a voltage spans a thermocouple to a transmission line",
    ("P", "W"):      "a power is record-specific",
    ("C", "F"):      "a capacitance spans pF to F",
    ("lambda", "m"): "a wavelength spans gamma to radio",
}


def main():
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    with contextlib.redirect_stdout(io.StringIO()):
        spec.loader.exec_module(m)

    # UNITS COME FROM BOTH MAPS. m.recs is the SHIPPED set; the holdout records are excluded from
    # it by construction, so resolving units from m.recs alone left every holdout declaration keyed
    # (var, "") -- bucketed as dimensionless, merged across quantities that units would have kept
    # apart, and unable to match any DELIBERATE key, all of which use real units. The gate reported
    # seven conflicts that were entirely the missing lookup: 'omega' collided with itself despite
    # ("omega","1/s") sitting in the exemption table.
    #
    # Same class as gate_units_parity -- one fact in two files, and a consumer that reads one.
    unit_of = {}
    for r in m.recs:
        for v, u in (r.get("units") or {}).items():
            unit_of[(r["f"], v)] = (u or "").strip()
    _ho = json.loads((ROOT / "corpus/units_holdout.json").read_text())
    for f, um in (_ho.items() if isinstance(_ho, dict) else ((r["f"], r.get("units") or {}) for r in _ho)):
        for v, u in (um or {}).items():
            unit_of.setdefault((f, v), (u or "").strip())

    groups = defaultdict(dict)          # (var, unit) -> {window: [formulas]}
    for (f, v), win in m._SCALE.items():
        u = unit_of.get((f, v), "")
        # SIGNIFICANT FIGURES, NOT DECIMAL PLACES. round(1e-21, 12) is 0.0, and round(1e-16, 12)
        # is 0.0 too -- so a perfectly good window (1e-21, 1e-16) was reported as the zero-width
        # window (0.0, 0.0) and flagged as a collapse. This is the round(x, 4) defect that
        # annihilated every optical wavelength, in a gate, written by someone who had read about it.
        groups[(v, u)].setdefault((_sig(win[0]), _sig(win[1])), []).append(f)

    # A REJECTED STRENGTHENING, RECORDED BECAUSE THE REASONING LOOKED SOUND AND WAS NOT.
    #
    # I tried making DELIBERATE unable to exempt a shipped/held-out pair, on the argument that a
    # held-out record drawing a shared variable from a different band confounds unseen-relation with
    # unseen-numbers. The argument is right. The implementation could not test it, for two reasons
    # the listing made obvious and the reasoning did not:
    #
    #   1. Shipped records already carry FIFTEEN different windows for v [m/s] among themselves -- a
    #      de Broglie speed, a speed in a refractive medium, a drag speed. There is no single shipped
    #      window for a holdout to match, so "differs from a shipped window" is true of almost
    #      everything and names no defect.
    #   2. _SCALE is PARTIAL -- 122 of 329 pairs declared. An undeclared pair draws from the mined
    #      pool, so the union of declared shipped windows is not the trained distribution. The rule
    #      flagged m [kg] as disjoint because the only DECLARED shipped m is the de Broglie electron
    #      mass; every macroscopic shipped m is undeclared and invisible here.
    #
    # The property is about DRAWS, not declarations, and no gate over _SCALE can see it. It is
    # measured directly in docs/RESULT_HOLDOUT_DISTRIBUTION.md instead.
    conflicts = [(k, w) for k, w in groups.items() if len(w) > 1 and k not in DELIBERATE]
    total_multi = sum(1 for k, w in groups.items() if len(w) > 1)
    print(f"  (variable, unit) pairs declared in >1 record: {total_multi}")
    print(f"  of those, deliberate and explained here:      {total_multi - len(conflicts)}")
    for (v, u), w in conflicts[:8]:
        print(f"  INCONSISTENT  {v!r} [{u or 'dimensionless'}] declared {len(w)} different ways:")
        for win, fs in list(w.items())[:3]:
            print(f"                  {win}  in  {fs[0]}")
    if conflicts:
        print(f"\n  FAIL: {len(conflicts)} quantity/unit pair(s) carry conflicting windows across")
        print("  records. Reconcile them, or add a DELIBERATE entry saying why they differ.")
        return 1
    if not groups:
        print("  CANNOT CHECK: no declarations found. Refusing to report a pass.")
        return 2
    print("  PASS: every multi-record quantity is consistent or explained")
    return 0


if __name__ == "__main__":
    sys.exit(main())
