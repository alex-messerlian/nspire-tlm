#!/usr/bin/env python3
"""Synthetic document generator + diversity metrics.

Token count is the wrong instrument: 310k documents from 27 record heads is 27 patterns repeated,
and the token count looks identical to a genuinely varied corpus. Everything here is reported
alongside three diversity measures, never alone."""
import json
import math
import sys as _sys, pathlib as _pl
_sys.path.insert(0, str(_pl.Path(__file__).parent))
from atomic import write_json as _wj, write_text as _wt
import json, re, random, subprocess, collections, math, sys, time, pathlib

# Universal constants are NOT free variables. The hand-read of 30 found 8 documents assigning
# random values to h, c, k_e, a_0, epsilon_0 and mu_0 -- teaching that Planck's constant is 36.
# They are supplied by the record (PROMPT_FORMAT section 2) and never sampled.
CONST = {
 "h":6.626e-34, "hbar":1.055e-34, "c":2.998e8, "G":6.674e-11, "k_e":8.988e9, "k":8.988e9,
 "epsilon_0":8.854e-12, "mu_0":1.257e-6, "a_0":5.292e-11, "N_A":6.022e23, "R":8.314,
 "sigma":5.670e-8, "g":9.81, "e":1.602e-19, "m_e":9.109e-31, "m_p":1.673e-27,
}

# ---- CONSTANTS RESOLVE PER RECORD, NOT PER NAME -------------------------------------------------
#
# `CONST[v]` is a global name->meaning table with no scope argument -- the SEVENTH instance of the
# pattern docs/IDENTIFIER_COLLISION.md documents, in the file that produces every training document,
# against a rule that document already states as enforceable: "any function mapping a name to a
# meaning must take a scope argument."
#
# Measured, by replaying this substitution over the record set: 9 of 77 productive heads got a
# DIFFERENT PHYSICAL QUANTITY. h := Planck's constant where the record declares metres of depth.
# R := the molar gas constant where the record declares ohms, and again where it declares a radius.
# c := the speed of light where the record declares J/(kg*K). That is every DC-circuit relation in
# the corpus, all of rolling motion, hydrostatics and calorimetry.
#
# The scope is the RECORD, and it has been carrying the answer all along: store_clean.json's `cval`
# field says which of a relation's symbols are supplied constants and what they are worth. Two rules:
#   1. If the record names the constant in its own cval, use THAT.
#   2. Otherwise fall back to CONST only when the record's DECLARED UNIT is dimensionally the
#      constant's unit. A mismatch means the symbol is a variable that happens to share a name.
# A record with no unit for the symbol gets no substitution -- absence is not permission.
_CUNIT = {"h":"J*s","hbar":"J*s","c":"m/s","G":"N*m^2/kg^2","k_e":"N*m^2/C^2","k":"N*m^2/C^2",
          "epsilon_0":"F/m","mu_0":"T*m/A","a_0":"m","N_A":"1/mol","R":"J/(mol*K)",
          "sigma":"W/(m^2*K^4)","g":"m/s^2","e":"C","m_e":"kg","m_p":"kg"}

def _dim_same(u1, u2):
    """Dimensional equality decided by the SHIPPED evaluator, not by string comparison -- `F/m` and
    `C^2/(N*m^2)` are the same dimension written two ways, and a string rule would reject the
    legitimate epsilon_0 records along with the illegitimate R ones."""
    if not u1 or not u2: return False
    if u1.replace(" ","") == u2.replace(" ",""): return True
    p = subprocess.run(["tools/eval/evalcli", f"<tool>conv<arg>1 {u1}<arg>{u2}</tool>"],
                       capture_output=True, text=True)
    return "!" not in p.stdout

_CONST_CACHE = {}
def _const_for(rec, v):
    """The value of `v` in `rec` if it is a supplied constant there, else None."""
    key = (rec.get("f"), v)
    if key in _CONST_CACHE: return _CONST_CACHE[key]
    out = None
    # A22b: a microscopic constant of the named particle or material. Declared in _MICRO_CVAL below
    # rather than sampled, because the empirical pool cannot reach 1.6e-19 at all. Resolved here so
    # A7 inlines it into the question -- the model must READ the value, not recall it.
    micro = globals().get("_MICRO_CVAL", {}).get((rec.get("f"), v))
    if micro is not None:
        _CONST_CACHE[key] = micro[0]
        return micro[0]
    cv = (_store.get(rec.get("f"), {}) or {}).get("cval") or {}
    if v in cv:
        try: out = float(cv[v])
        except (TypeError, ValueError): out = None
    elif v in CONST:
        u = (rec.get("units") or {}).get(v)
        out = CONST[v] if (u and _dim_same(u, _CUNIT[v])) else None
    _CONST_CACHE[key] = out
    return out

_EMP = json.load(open("corpus/empirical_values.json"))

# A22a. THE POOL IS KEYED BY UNIT AND THE FLATTEN THREW THE KEY AWAY.
# `sorted(v for vs in _EMP.values() for v in vs)` merged 29 unit-keyed lists into one, so a mass
# could be drawn from the charge list and a length from the frequency list. Keeping the key is
# strictly better and costs nothing -- the data was always shaped for it.
_POOL_BY_UNIT = {k: sorted(float(v) for v in vs) for k, vs in _EMP.items()}
_POOL = sorted(v for vs in _POOL_BY_UNIT.values() for v in vs)

# A22b. AND THE POOL CANNOT REACH A MICROSCOPIC VALUE AT ALL. Every list is mined from OpenStax
# PROBLEM VALUES, so the whole pool spans [0.001, 9800] with median 8. For any quantity whose
# physical scale lies outside that window there is NO admissible draw -- not a bad tail, an
# inability. Measured consequences, each 100% of the documents on those records:
#
#   v_d=I/(n*q*A)   carrier density 4,186 /m^3   (copper: 8.5e28)      -- wrong by 25 orders
#                   carrier charge 2 C           (elementary: 1.6e-19) -- wrong by 19 orders
#   r=(m*v)/(q*B)   "a particle" of 500 kg carrying 12 C
#   Delta_y=x*lambda/d   a 6,000 m wavelength through slits 1 m apart
#
# These variables are not free parameters, they are PHYSICAL CONSTANTS of the named particle or
# material. The store already has the mechanism -- `cval`, which _const_for() resolves and A7
# inlines into the question so the model reads the value rather than recalling it. Supplying them
# here is what the mechanism is for.
_MICRO_CVAL = {
    # (formula, variable) -> (value, unit, what it is)
    ("V=((k_e*q)/(r))", "k_e"): (8.988e9, "V*m/C", "Coulomb constant"),
    # A27. ATMOSPHERIC PRESSURE IS A CONSTANT, and the pool cannot reach it: 1.013e5 Pa is ABOVE
    # the pool's maximum of 9,800, so `p_0 = 1` Pa was drawn beside a record asserting "standard
    # conditions" -- 100.0% of documents on both records. Declared, so A7 inlines it and the model
    # reads the value.
    ("p_abs=p_g+p_atm", "p_atm"):  (1.013e5, "Pa", "standard atmospheric pressure"),
    ("p=p_0+rho*g*h", "p_0"):      (1.013e5, "Pa", "atmospheric pressure at the surface"),
    ("v_d=((I)/(n*q*A))", "q"): (1.602e-19, "C",     "elementary charge"),
    ("v_d=((I)/(n*q*A))", "n"): (8.5e28,    "1/m^3", "conduction-electron density in copper"),
    ("T=((2*pi*m)/(q*B))", "q"): (1.602e-19, "C",    "elementary charge"),
    ("T=((2*pi*m)/(q*B))", "m"): (9.109e-31, "kg",   "electron mass"),
    ("r=((m*v)/(q*B))",   "q"): (1.602e-19, "C",     "elementary charge"),
    ("r=((m*v)/(q*B))",   "m"): (9.109e-31, "kg",    "electron mass"),
}

def sample_value(rng, unit=None):
    """Draw from the empirical OpenStax distribution: median ~5, 60-90% round numbers.
    uniform(1.5, 95) produced m = 92.6 kg and r = 70.25 m, which no textbook contains.

    KEYED BY UNIT where the data has that unit -- see A22a. Falls back to the merged pool for a
    unit the mining never saw, which is the pre-existing behaviour and is stated rather than
    hidden: a fallback that looks like a choice is how the flatten went unnoticed."""
    pool = _POOL_BY_UNIT.get(unit) if unit else None
    return rng.choice(pool if pool else _POOL)


# A21. THE GIVENS WERE UNCHECKED. A17 filters RESULTS, and every input was drawn from one pool
# regardless of what the variable IS -- so 69% of documents on the six trigonometric records fed an
# angle of, for instance, 55.7 radians, which is 8.9 full turns. cos of it is a number, the
# evaluator computes it, the result is dimensionally fine and every gate passed.
#
# THE RANGE IS PER QUANTITY, NOT PER UNIT. The declared unit cannot decide this: `1` covers angles,
# counts, quantum numbers, refractive indices and plain ratios, and their admissible ranges differ
# by orders of magnitude. So the class is identified from the VARIABLE and the RECORD together.
#
# NARROW AND EXPLICIT, and the limit is the honest part: a variable this table does not recognise
# is drawn as before. It is a list of quantities somebody has thought about, not a theory of
# physical plausibility, and "not in the table" must not be read as "checked".
# A24. DECLARATION, NOT INFERENCE. THE CHAIN COULD DISPATCH TO THE WRONG RULE.
#
# quantity_range() was an ordered chain, first match wins, with an angle test on top asking whether
# a variable sits inside a trig call's argument. The extractor was `\(\s*([^)]*)\)` and `[^)]*`
# cannot match balanced parentheses, so on Snell's law
#
#     theta_2 = asin(((n_1*sin(theta_1))/(n_2)))
#
# it captured `((n_1*sin(theta_1)` and stopped. n_1 looked like a trig argument and got the ANGLE
# window [0, 2*pi] -- while THE CORRECT RULE FOR IT, refractive index [1,4], sat twelve lines below
# and was never reached. Measured: n_1 < 1 on 13.3% of Snell documents against 0% for n_2, the same
# quantity in the same expression. n_1 = 0.019 asserts light at 52c inside the medium.
#
# DIFFERENT FROM EVERY OTHER FAILURE IN A6-A23. Those are ABSENCES -- a window never declared -- and
# each is fixed by adding a row. This is a PRESENCE THAT IS WRONG: the row is there, and an audit
# asking "does this variable have a window?" answers yes and moves on.
#
# AND THE OBVIOUS REPAIR INVERTS IT: with a balanced-paren extractor BOTH n_1 and n_2 sit inside
# asin's argument, so both become angles. n_2 is correct today only because of the bug. Being
# INSIDE a trig argument does not make a variable an angle; the heuristic is unsalvageable.
#
# So the inference is gone. A kind is DECLARED per (record, variable), or the unit fixes it
# soundly, or it is UNCHECKED and counted. No chain remains to misdispatch, and the only failure
# left is ABSENCE -- which is countable, and the count is printed on every gate run.

_KIND_RANGE = {          # one range per kind, so two variables of a kind cannot drift apart
    "angle_quadrant":   (0.0, 1.5708,  False, "an incidence, incline or launch angle is in [0, pi/2]"),
    "angle_half":       (0.0, 3.14159, False, "an angle between two directions is in [0, pi]"),
    "angle_turn":       (0.0, 6.28318, False, "an angle within one full turn"),
    "angle_arc":        (0.0, 12.5664, False, "a swept angle, up to two full turns"),
    "refractive_index": (1.0, 4.0,     False, "a refractive index below 1 implies light faster than c"),
    "quantum_number":   (1.0, 12.0,    True,  "a quantum number is a small positive integer"),
    "turns_count":      (1.0, 5000.0,  True,  "a transformer winding count is a positive integer"),
    # (3, 7) admits 4, and no gas molecule has four degrees of freedom: the physical set is
    # {3 monatomic, 5 diatomic, 6 polyatomic}. A range cannot express a set, so the window is
    # narrowed to the diatomic/polyatomic pair and 3 is reached by the monatomic case below.
    "dof":              (5.0, 6.0,     True,  "degrees of freedom: 5 diatomic, 6 polyatomic"),
    "drag_coefficient": (0.04, 2.0,    False, "from a streamlined body to a flat plate"),
}

# Every DIMENSIONLESS sampled variable in the store, which is exactly where the unit cannot decide:
# `1` covers angles, counts, indices and bare ratios. This is the complete enumeration; the
# assertion further down fails if an entry stops matching a real (record, variable) pair.
_KIND = {
    ("E_n=-E_0*((1)/((n)^(2)))", "n"):                       "quantum_number",
    ("K=(n)^(2)*E_1", "n"):                                  "quantum_number",
    ("E=n*h*f", "n"):                                        "quantum_number",
    ("lambda_n=((lambda)/(n))", "n"):                        "refractive_index",
    ("h_i=(((n_2)/(n_1)))*h_o", "n_1"):                      "refractive_index",
    ("h_i=(((n_2)/(n_1)))*h_o", "n_2"):                      "refractive_index",
    ("theta_2=asin(((n_1*sin(theta_1))/(n_2)))", "n_1"):     "refractive_index",
    ("theta_2=asin(((n_1*sin(theta_1))/(n_2)))", "n_2"):     "refractive_index",
    ("theta_2=asin(((n_1*sin(theta_1))/(n_2)))", "theta_1"): "angle_quadrant",
    ("theta_r=theta_i", "theta_i"):                          "angle_quadrant",
    ("Delta_l=d*sin(theta)", "theta"):                       "angle_quadrant",
    ("N=m*g*cos(theta)", "theta"):                           "angle_quadrant",
    ("T_tof=((2(v_0*sin(theta_0)))/(g))", "theta_0"):        "angle_quadrant",
    ("a_CM=((m*g*sin(theta))/(m+(I_CM/(r)^(2))))", "theta"): "angle_quadrant",
    ("F=q*v*B*sin(theta)", "theta"):                         "angle_half",
    ("A=((1)/(2))*theta*(r)^(2)", "theta"):                  "angle_turn",
    # A visual angle subtended at the eye is at most pi, not a full turn.
    ("M=((theta_image)/(theta_object))", "theta_image"):     "angle_half",
    ("M=((theta_image)/(theta_object))", "theta_object"):    "angle_half",
    ("s=r*theta", "theta"):                                  "angle_arc",
    ("d_CM=R*theta", "theta"):                               "angle_arc",
    ("omega=((theta)/(t))", "theta"):                        "angle_arc",
    ("omega=((Delta_theta)/(Delta_t))", "Delta_theta"):      "angle_arc",
    ("C_V=((d)/(2))*R", "d"):                                "dof",
    ("I_S=((N_P)/(N_S))*I_P", "N_P"):                        "turns_count",
    ("I_S=((N_P)/(N_S))*I_P", "N_S"):                        "turns_count",
    ("F_D=((1)/(2))*C*rho*A*(v)^(2)", "C"):                  "drag_coefficient",
}

# Units that fix the quantity ON THEIR OWN -- no inference. Ambiguous units (1, m, J, Hz, m/s) are
# absent on purpose: m is a wavelength or a slit or a radius, and the unit cannot say which.
_UNIT_RANGE = {
    "rad": (0.0, 6.28318, False, "an angle within one full turn"),
    "kg":  (1.0e-6, 1.0e6, False, "a laboratory mass"),
    "T":   (1.0e-5, 100.0, False, "a magnetic field beyond 100 T has never been produced"),
}


# A23. THE SCALE OF A QUANTITY IS PHYSICS, AND NO FORMULA IMPLIES IT. A22 supplied six microscopic
# constants and left the rest of the class standing: the mined pool spans [0.001, 9800] and for
# ELEVEN of thirteen physical windows it contains NO admissible value at all -- a photon frequency,
# an atomic level difference, an optical wavelength, a nuclear mass defect. Eighteen records
# measured at EXACTLY 100.0% wrong, which is the signature of an inability.
#
# A22 was per-(record, variable) whack-a-mole. This is the class: every record carrying a quantum
# or electromagnetic constant has FREE VARIABLES THAT MUST LIVE AT THAT CONSTANT'S SCALE, and the
# scale cannot be derived -- that a photon energy is ~1e-19 J is knowledge about the world, not a
# consequence of E = h*f.
#
# So it is declared, per record, with the justification attached. Only RHS variables are sampled,
# so this is one or two entries per record rather than one per variable. It belongs in the store
# beside `cval` eventually; it is here because that is where quantity_range already reads.
#
# EVERY ENTRY IS A CLAIM ABOUT PHYSICS AND IS WRITTEN TO BE CHECKED. A range that is wrong is a
# defect of exactly the kind this table exists to remove -- see the Fahrenheit repair, which was a
# fix that asserted something false.
_SCALE = {
  # --- photons and atomic transitions -------------------------------------------------------
  ("E_f=h*f", "f"):                 (1e12, 1e19, "IR through X-ray photon frequency"),
  ("E=h*f", "f"):                   (1e12, 1e19, "IR through X-ray photon frequency"),
  ("Delta_E=h*f", "f"):             (1e12, 1e19, "IR through X-ray photon frequency"),
  ("E=n*h*f", "f"):                 (1e12, 1e19, "IR through X-ray photon frequency"),
  ("Delta_E=h*Delta_f", "Delta_f"): (1e6,  1e15, "a spectroscopic frequency difference"),
  ("E_b=h*f_O", "f_O"):             (1e14, 1e16, "photoelectric threshold, 0.4-40 eV"),
  ("f=(Delta_E_LK)/h", "Delta_E_LK"): (1.6e-19, 1.6e-15, "atomic level difference, 1 eV - 10 keV"),
  ("Delta_t=((h)/(E))", "E"):       (1e-19, 1e-12, "virtual-particle energy, sub-MeV"),
  ("T_F=((E_F)/(k_B))", "E_F"):     (1.6e-19, 1.6e-17, "Fermi energy, 1-100 eV"),
  ("lambda=((h*c)/(E))", "E"):      (1.6e-19, 1.6e-14, "photon energy, 1 eV - 100 keV"),
  ("E=((h*c)/(lambda))", "lambda"): (1e-12, 1e-6,  "X-ray through infrared wavelength"),
  ("p=((h)/(lambda))", "lambda"):   (1e-12, 1e-6,  "X-ray through infrared wavelength"),
  ("lambda=((h)/(p))", "p"):        (1e-27, 1e-20, "momentum of an atomic-scale particle"),
  ("m=Delta_E/(c)^(2)", "Delta_E"): (1e-15, 1e-10, "nuclear binding energy, keV - MeV"),
  ("E=(Delta_m)(c)^(2)", "Delta_m"): (1e-30, 1e-26, "nuclear mass defect"),
  ("lambda=((h)/(m*v))", "m"):      (1e-31, 1e-25, "electron through heavy-ion mass"),
  ("lambda=((h)/(m*v))", "v"):      (1e2,  1e7,   "non-relativistic particle speed"),
  # --- light and optics ---------------------------------------------------------------------
  ("f=((c)/(lambda))", "lambda"):   (1e-12, 1e-1,  "X-ray through radio wavelength"),
  ("lambda=((c)/(f))", "f"):        (1e6,  1e18,  "radio through X-ray frequency"),
  ("n=((c)/(v))", "v"):             (1e8,  2.998e8, "light in a medium: n in [1, 3]"),
  ("Delta_y=x*lambda/d", "lambda"): (1e-9, 1e-5,  "optical wavelength"),
  ("Delta_y=x*lambda/d", "d"):      (1e-6, 1e-2,  "slit separation, sub-centimetre"),
  ("Delta_y=x*lambda/d", "x"):      (0.1,  10.0,  "screen distance, laboratory scale"),
  ("Delta_l=d*sin(theta)", "d"):    (1e-6, 1e-2,  "slit separation, sub-centimetre"),
  # --- electromagnetism ----------------------------------------------------------------------
  ("V=((k_e*q)/(r))", "q"):         (1e-9, 1e-3,  "a laboratory charge, nC to mC"),
  ("V=((k_e*q)/(r))", "r"):         (1e-3, 10.0,  "laboratory distance"),
  ("E=((sigma)/(epsilon_0))", "sigma"): (1e-9, 1e-3, "surface charge density, nC/m^2 to mC/m^2"),
  ("C=epsilon_0*((A)/(d))", "A"):   (1e-4, 1.0,   "plate area, cm^2 to m^2"),
  ("C=epsilon_0*((A)/(d))", "d"):   (1e-6, 1e-3,  "plate gap, micrometres to a millimetre"),
  ("B=((mu_0*I)/(2*pi*R))", "I"):   (1e-2, 1e3,   "a laboratory current"),
  ("B=((mu_0*I)/(2*pi*R))", "R"):   (1e-3, 1.0,   "distance from a wire, laboratory scale"),
  ("v_d=((I)/(n*q*A))", "A"):       (1e-8, 1e-4,  "wire cross-section, 0.1-10 mm^2"),
  # --- gravitation, astronomical by convention in this relation -------------------------------
  ("F=G*m1*m2/(r)^(2)", "m1"):      (1e20, 1e30,  "planetary or stellar mass"),
  ("F=G*m1*m2/(r)^(2)", "m2"):      (1e20, 1e30,  "planetary or stellar mass"),
  ("F=G*m1*m2/(r)^(2)", "r"):       (1e6,  1e12,  "orbital separation"),
  # --- relativistic Doppler --------------------------------------------------------------------
  ("f_obs=f_s*sqrt(((1-((v)/(c)))/(1+((v)/(c)))))", "v"): (1e3, 2.9e8, "sub-luminal source speed"),
  # --- A27: quantities the pool cannot reach, measured at EXACTLY 100% in Step 0 run 6 ---------
  ("F=q*v*B", "q"):                 (1e-9, 1e-3, "a laboratory charge, nC to mC"),
  ("F=q*v*B*sin(theta)", "q"):      (1e-9, 1e-3, "a laboratory charge, nC to mC"),
  ("V=((U_E)/(q))", "q"):           (1e-9, 1e-3, "a laboratory charge, nC to mC"),
  ("C=((Q)/(V))", "Q"):             (1e-9, 1e-3, "a laboratory charge, nC to mC"),
  ("E_n=-E_0*((1)/((n)^(2)))", "E_0"): (1.6e-19, 1e-17, "an atomic ground state, ~13.6 eV"),
  ("K=(n)^(2)*E_1", "E_1"):         (1e-21, 1e-18, "a particle-in-a-box ground state"),
  ("n=((K/E_1))^(1/2)", "E_1"):     (1e-21, 1e-18, "a particle-in-a-box ground state"),
  ("n=((K/E_1))^(1/2)", "K"):       (1e-21, 1e-16, "an energy level of the same well"),
  ("U_form=E_transfer+U_coul+U_ex", "E_transfer"): (1e-19, 1e-17, "an ionic-bond energy term"),
  ("U_form=E_transfer+U_coul+U_ex", "U_coul"):     (1e-19, 1e-17, "an ionic-bond energy term"),
  ("U_form=E_transfer+U_coul+U_ex", "U_ex"):       (1e-19, 1e-17, "an ionic-bond energy term"),
  # ---- A29: the remaining 193 pairs, authored per physics domain and reviewed ------------------
  # Closing the coverage in one pass instead of discovering ~15 per Step 0 round. Six authors split
  # by domain, then a physics reviewer rejected 7 proposals, corrected 14, and -- the valuable part
  # -- found EIGHT DEFECTS IN THE 133 DECLARATIONS THAT ALREADY EXISTED, which nobody had checked.
  #
  # ONE PAIR IS DELIBERATELY LEFT UNDECLARED: ("T=((1)/(f))", "f"). Every floor anyone could justify
  # is falsified by a real periodic system a textbook uses -- the 67 m Foucault pendulum is 0.061 Hz,
  # a tide is 2.3e-5 Hz -- and every value the pool draws is a real period. No defect to close and no
  # honest window, so it stays counted as UNCHECKED. That is the intended use of UNCHECKED.
  ("F=-k*x", "k"): (1.0, 1.0e5, "a real spring: ~1 N/m for a soft coil to ~10^5 N/m for a car suspension"),
  ("F=-k*x", "x"): (0.001, 1.0, "an elastic displacement: a millimetre to a metre, beyond which a coil spring is past its limit"),
  ("k=((F)/(x))", "F"): (0.1, 10000.0, "the force a hand or a hanging mass applies to a spring"),
  ("k=((F)/(x))", "x"): (0.001, 1.0, "an elastic displacement: a millimetre to a metre, beyond which a coil spring is past its limit"),
  ("omega=sqrt(((k)/(m)))", "k"): (1.0, 1.0e5, "a real spring: ~1 N/m for a soft coil to ~10^5 N/m for a car suspension"),
  ("f=((1)/(2*pi))*sqrt(((k)/(m)))", "k"): (1.0, 1.0e5, "a real spring: ~1 N/m for a soft coil to ~10^5 N/m for a car suspension"),
  ("T=2*pi*sqrt(((m)/(k)))", "k"): (1.0, 1.0e5, "a real spring: ~1 N/m for a soft coil to ~10^5 N/m for a car suspension"),
  ("U=((1)/(2))*m*(omega)^(2)*(x)^(2)", "omega"): (0.1, 10000.0, "an oscillator angular frequency: a pendulum at ~3 rad/s to an ultrasonic transducer at 1e4 rad/s"),
  ("U=((1)/(2))*m*(omega)^(2)*(x)^(2)", "x"): (0.001, 1.0, "an oscillation amplitude: a millimetre to a metre"),
  ("g=((4*(pi)^(2)*L)/((T)^(2)))", "L"): (0.05, 70.0, "a pendulum length: a 5 cm laboratory bob to the 67 m Foucault pendulum"),
  ("x=l-l_0", "l"): (0.01, 100.0, "a spring, rod or cable length: a centimetre to a hundred metres"),
  ("x=l-l_0", "l_0"): (0.01, 100.0, "a spring, rod or cable length: a centimetre to a hundred metres"),
  ("F=m*a", "a"): (0.05, 300.0, "a linear acceleration from a gentle lift at 0.05 m/s^2 to a 30 g crash deceleration at 300 m/s^2"),
  ("F_net=m*a", "a"): (0.05, 300.0, "a linear acceleration from a gentle lift at 0.05 m/s^2 to a 30 g crash deceleration at 300 m/s^2"),
  ("m=F_net/a", "a"): (0.05, 300.0, "a linear acceleration from a gentle lift at 0.05 m/s^2 to a 30 g crash deceleration at 300 m/s^2"),
  ("m=F_net/a", "F_net"): (0.1, 1.0e5, "a mechanics force: the weight of a paperclip at ~0.1 N to a collision or engine force at 10^5 N"),
  ("a=((F)/(m))", "F"): (0.1, 1.0e5, "a mechanics force: the weight of a paperclip at ~0.1 N to a collision or engine force at 10^5 N"),
  ("F_c=m*a_c", "a_c"): (0.1, 10000.0, "a centripetal acceleration from 0.1 m/s^2 on a slow carousel to 1e4 m/s^2 in a bench centrifuge (~1000 g)"),
  ("v=v_0+a*t", "a"): (0.05, 300.0, "a linear acceleration from a gentle lift at 0.05 m/s^2 to a 30 g crash deceleration at 300 m/s^2"),
  ("v=v_0+a*t", "v_0"): (0.01, 1000.0, "a textbook mechanical speed: a snail at 0.013 m/s to a rifle bullet at 900 m/s"),
  ("v=a*t", "a"): (0.05, 300.0, "a linear acceleration from a gentle lift at 0.05 m/s^2 to a 30 g crash deceleration at 300 m/s^2"),
  ("a=((Delta_v)/(Delta_t))", "Delta_v"): (0.01, 1000.0, "a change of speed over the interval, on the scale of the speeds themselves"),
  ("a=(vf-vi)/t", "vf"): (0.01, 1000.0, "a textbook mechanical speed: a snail at 0.013 m/s to a rifle bullet at 900 m/s"),
  ("a=(vf-vi)/t", "vi"): (0.01, 1000.0, "a textbook mechanical speed: a snail at 0.013 m/s to a rifle bullet at 900 m/s"),
  ("v=d/t", "d"): (0.001, 1.0e6, "a distance travelled: a bench-top millimetre to a 1000 km journey"),
  ("Delta_x=x_f-x_i", "x_f"): (0.001, 10000.0, "a position on a laboratory or field axis"),
  ("Delta_x=x_f-x_i", "x_i"): (0.001, 10000.0, "a position on a laboratory or field axis"),
  ("x=x_0+v_x*t", "v_x"): (0.01, 1000.0, "a textbook mechanical speed: a snail at 0.013 m/s to a rifle bullet at 900 m/s"),
  ("x=x_0+v_x*t", "x_0"): (0.001, 10000.0, "a starting position on a laboratory or field axis"),
  ("d=d_0+v_0*t+((1)/(2))*a*(t)^(2)", "a"): (0.05, 300.0, "a linear acceleration from a gentle lift at 0.05 m/s^2 to a 30 g crash deceleration at 300 m/s^2"),
  ("d=d_0+v_0*t+((1)/(2))*a*(t)^(2)", "d_0"): (0.001, 10000.0, "a starting position on a laboratory or field axis"),
  ("d=d_0+v_0*t+((1)/(2))*a*(t)^(2)", "v_0"): (0.01, 1000.0, "a textbook mechanical speed: a snail at 0.013 m/s to a rifle bullet at 900 m/s"),
  ("d=vi*t+0.5*a*(t)^(2)", "a"): (0.05, 300.0, "a linear acceleration from a gentle lift at 0.05 m/s^2 to a 30 g crash deceleration at 300 m/s^2"),
  ("d=vi*t+0.5*a*(t)^(2)", "vi"): (0.01, 1000.0, "a textbook mechanical speed: a snail at 0.013 m/s to a rifle bullet at 900 m/s"),
  ("d=sqrt(((x_2-x_1))^(2)+((y_2-y_1))^(2))", "x_1"): (0.001, 10000.0, "a map or laboratory coordinate"),
  ("d=sqrt(((x_2-x_1))^(2)+((y_2-y_1))^(2))", "x_2"): (0.001, 10000.0, "a map or laboratory coordinate"),
  ("d=sqrt(((x_2-x_1))^(2)+((y_2-y_1))^(2))", "y_1"): (0.001, 10000.0, "a map or laboratory coordinate"),
  ("d=sqrt(((x_2-x_1))^(2)+((y_2-y_1))^(2))", "y_2"): (0.001, 10000.0, "a map or laboratory coordinate"),
  ("v_toty=v_wy+v_p", "v_p"): (1.0, 300.0, "the craft's own speed: a 1 m/s boat to a 300 m/s airliner"),
  ("v_toty=v_wy+v_p", "v_wy"): (0.1, 50.0, "a wind or current component: a 0.1 m/s drift to a 50 m/s gale"),
  ("v=v_0-g*t", "v_0"): (0.5, 300.0, "a launch speed under gravity: a gently tossed ball at 0.5 m/s to a rifle bullet at 300 m/s"),
  ("y=y_0+v_0*t-((1)/(2))*g*(t)^(2)", "v_0"): (0.5, 300.0, "a launch speed under gravity: a gently tossed ball at 0.5 m/s to a rifle bullet at 300 m/s"),
  ("y=y_0+v_0*t-((1)/(2))*g*(t)^(2)", "y_0"): (0.01, 1000.0, "a launch height: ground level to a 1 km cliff or tower"),
  ("y=y_0+((1)/(2))(v_0y+v_y)t", "v_0y"): (0.01, 300.0, "a vertical velocity component under gravity, up to a rifle-bullet launch"),
  ("y=y_0+((1)/(2))(v_0y+v_y)t", "v_y"): (0.01, 300.0, "a vertical velocity component under gravity, up to a rifle-bullet launch"),
  ("y=y_0+((1)/(2))(v_0y+v_y)t", "y_0"): (0.01, 1000.0, "a launch height: ground level to a 1 km cliff or tower"),
  ("x=v_0x*t", "v_0x"): (0.5, 300.0, "the horizontal launch component of a projectile"),
  ("T_tof=((2(v_0*sin(theta_0)))/(g))", "v_0"): (0.5, 300.0, "a launch speed under gravity: a gently tossed ball at 0.5 m/s to a rifle bullet at 300 m/s"),
  ("p=m*v", "v"): (0.01, 1000.0, "a textbook mechanical speed: a snail at 0.013 m/s to a rifle bullet at 900 m/s"),
  ("Delta_p=m*Delta_v", "Delta_v"): (0.01, 1000.0, "a change of speed in a collision or braking event, on the scale of the speeds themselves"),
  ("F=((m*Delta_v)/(Delta_t))", "Delta_v"): (0.01, 1000.0, "a change of speed in a collision or braking event, on the scale of the speeds themselves"),
  ("F_net=((Delta_p)/(Delta_t))", "Delta_p"): (0.01, 1.0e5, "a momentum change from 0.01 kg m/s (a flicked coin) to 1e5 kg m/s (a loaded truck)"),
  ("Delta_p=F_net*Delta_t", "F_net"): (0.1, 1.0e5, "a mechanics force: the weight of a paperclip at ~0.1 N to a collision or engine force at 10^5 N"),
  ("W=F*d", "F"): (0.1, 1.0e5, "a mechanics force: the weight of a paperclip at ~0.1 N to a collision or engine force at 10^5 N"),
  ("W=F*d", "d"): (0.001, 10000.0, "a laboratory-to-field distance: a millimetre to ten kilometres"),
  ("W_o=F_o*d_o", "F_o"): (0.1, 1.0e5, "a mechanics force: the weight of a paperclip at ~0.1 N to a collision or engine force at 10^5 N"),
  ("W_o=F_o*d_o", "d_o"): (0.01, 100.0, "the distance a machine's output force acts over"),
  ("W_i=F_i*d_i", "F_i"): (0.1, 1.0e5, "a mechanics force: the weight of a paperclip at ~0.1 N to a collision or engine force at 10^5 N"),
  ("W_i=F_i*d_i", "d_i"): (0.01, 100.0, "the distance a machine's input force acts over"),
  ("W_net=K_B-K_A", "K_A"): (0.01, 1.0e7, "a mechanical energy from a dropped coin at ~0.01 J to a loaded truck in motion at ~10^7 J"),
  ("W_net=K_B-K_A", "K_B"): (0.01, 1.0e7, "a mechanical energy from a dropped coin at ~0.01 J to a loaded truck in motion at ~10^7 J"),
  ("E=K+U", "K"): (0.01, 1.0e7, "a mechanical energy from a dropped coin at ~0.01 J to a loaded truck in motion at ~10^7 J"),
  ("E=K+U", "U"): (0.01, 1.0e7, "a mechanical energy from a dropped coin at ~0.01 J to a loaded truck in motion at ~10^7 J"),
  ("W_out=W_in-W_f", "W_f"): (0.01, 1.0e7, "a mechanical energy from a dropped coin at ~0.01 J to a loaded truck in motion at ~10^7 J"),
  ("W_out=W_in-W_f", "W_in"): (0.01, 1.0e7, "a mechanical energy from a dropped coin at ~0.01 J to a loaded truck in motion at ~10^7 J"),
  ("K=0.5*m*(v)^(2)", "v"): (0.01, 1000.0, "a textbook mechanical speed: a snail at 0.013 m/s to a rifle bullet at 900 m/s"),
  ("U=m*g*h", "h"): (0.01, 10000.0, "a height above the reference level; constant g needs h far below the Earth's 6371 km radius"),
  ("P=((W)/(t))", "W"): (0.01, 1.0e7, "a mechanical energy from a dropped coin at ~0.01 J to a loaded truck in motion at ~10^7 J"),
  ("P=F*v", "F"): (0.1, 1.0e5, "a mechanics force: the weight of a paperclip at ~0.1 N to a collision or engine force at 10^5 N"),
  ("P=F*v", "v"): (0.01, 1000.0, "a textbook mechanical speed: a snail at 0.013 m/s to a rifle bullet at 900 m/s"),
  ("E=P*t", "P"): (0.5, 1.0e6, "a mechanical or household power: a 0.5 W indicator to a 1 MW locomotive"),
  ("v_t=r*omega", "omega"): (1.0e-5, 10000.0, "a rotating body's angular speed: the Earth's spin at 7.3e-5 rad/s to a turbocharger at 10^4 rad/s"),
  ("v_t=r*omega", "r"): (0.01, 30.0, "a lever arm or rotor radius: a bolt head at 1 cm to a crane boom at 30 m"),
  ("omega=v/r", "v"): (0.01, 1000.0, "a textbook mechanical speed: a snail at 0.013 m/s to a rifle bullet at 900 m/s"),
  ("omega=v/r", "r"): (0.01, 30.0, "a lever arm or rotor radius: a bolt head at 1 cm to a crane boom at 30 m"),
  ("v_CM=R*omega", "R"): (0.01, 2.0, "a rolling body's radius: a marble at 1 cm to a large wheel or boulder at 2 m"),
  ("v_CM=R*omega", "omega"): (1.0e-5, 10000.0, "a rotating body's angular speed: the Earth's spin at 7.3e-5 rad/s to a turbocharger at 10^4 rad/s"),
  ("a_CM=R*alpha", "R"): (0.01, 2.0, "a rolling body's radius: a marble at 1 cm to a large wheel or boulder at 2 m"),
  ("a_CM=R*alpha", "alpha"): (1.0e-4, 10000.0, "an angular acceleration from a flywheel creeping up at 1e-4 rad/s^2 to a hard disc spinning up at 1e4 rad/s^2"),
  ("d_CM=R*theta", "R"): (0.01, 2.0, "a rolling body's radius: a marble at 1 cm to a large wheel or boulder at 2 m"),
  ("a_t=r*alpha", "r"): (0.01, 30.0, "a lever arm or rotor radius: a bolt head at 1 cm to a crane boom at 30 m"),
  ("a_t=r*alpha", "alpha"): (1.0e-4, 10000.0, "an angular acceleration from a flywheel creeping up at 1e-4 rad/s^2 to a hard disc spinning up at 1e4 rad/s^2"),
  ("alpha=((a_t)/(r))", "a_t"): (0.05, 10000.0, "a tangential acceleration of a point on a rotating body; the same window as the result of a_t=r*alpha, so it does not depend on which side of the relation it appears on"),
  ("alpha=((a_t)/(r))", "r"): (0.01, 30.0, "a lever arm or rotor radius: a bolt head at 1 cm to a crane boom at 30 m"),
  ("alpha=((Delta_omega)/(Delta_t))", "Delta_omega"): (1.0e-4, 10000.0, "a change of angular speed over the interval, on the scale of the speeds themselves"),
  ("omega_f=omega_0+alpha*t", "alpha"): (1.0e-4, 10000.0, "an angular acceleration from a flywheel creeping up at 1e-4 rad/s^2 to a hard disc spinning up at 1e4 rad/s^2"),
  ("omega_f=omega_0+alpha*t", "omega_0"): (1.0e-5, 10000.0, "a rotating body's angular speed: the Earth's spin at 7.3e-5 rad/s to a turbocharger at 10^4 rad/s"),
  ("s=r*theta", "r"): (0.01, 30.0, "a lever arm or rotor radius: a bolt head at 1 cm to a crane boom at 30 m"),
  ("theta=((s)/(r))", "r"): (0.01, 30.0, "a lever arm or rotor radius: a bolt head at 1 cm to a crane boom at 30 m"),
  ("theta=((s)/(r))", "s"): (0.001, 400.0, "an arc on a circle of the declared radius: two turns of the widest r (30 m) is 377 m"),
  ("Delta_theta=((Delta_s)/(r))", "r"): (0.01, 30.0, "a lever arm or rotor radius: a bolt head at 1 cm to a crane boom at 30 m"),
  ("Delta_theta=((Delta_s)/(r))", "Delta_s"): (0.001, 400.0, "an arc on a circle of the declared radius: two turns of the widest r (30 m) is 377 m"),
  ("A=((1)/(2))*theta*(r)^(2)", "r"): (0.01, 30.0, "the radius of the sector's circle"),
  ("L=I*omega", "I"): (1.0e-7, 1.0e5, "a moment of inertia: a spinning coin at ~4e-7 kg m^2 to a power-station flywheel at 1e5 kg m^2"),
  ("L=I*omega", "omega"): (1.0e-5, 10000.0, "a rotating body's angular speed: the Earth's spin at 7.3e-5 rad/s to a turbocharger at 10^4 rad/s"),
  ("P=tau*omega", "omega"): (1.0e-5, 10000.0, "a rotating body's angular speed: the Earth's spin at 7.3e-5 rad/s to a turbocharger at 10^4 rad/s"),
  ("P=tau*omega", "tau"): (0.01, 1.0e5, "a torque from a 0.01 N m watch spring to a 1e5 N m propeller shaft"),
  ("tau=r*F", "F"): (0.1, 1.0e5, "a mechanics force: the weight of a paperclip at ~0.1 N to a collision or engine force at 10^5 N"),
  ("tau=r*F", "r"): (0.01, 30.0, "a lever arm or rotor radius: a bolt head at 1 cm to a crane boom at 30 m"),
  ("a_CM=((m*g*sin(theta))/(m+(I_CM/(r)^(2))))", "r"): (0.01, 2.0, "a rolling body's radius: a marble at 1 cm to a large wheel or boulder at 2 m"),
  ("a=(v)^(2)/r", "v"): (0.01, 300.0, "the speed of a body on a circular path"),
  ("a=(v)^(2)/r", "r"): (0.01, 1000.0, "the radius of a circular path: a laboratory rotor at 1 cm to a motorway curve at 1 km"),
  ("a_c=(((v)^(2))/(r))", "v"): (0.01, 300.0, "the speed of a body on a circular path"),
  ("a_c=(((v)^(2))/(r))", "r"): (0.01, 1000.0, "the radius of a circular path: a laboratory rotor at 1 cm to a motorway curve at 1 km"),
  ("p=((F)/(A))", "F"): (0.1, 1.0e5, "a mechanics force: the weight of a paperclip at ~0.1 N to a collision or engine force at 10^5 N"),
  ("p=((F)/(A))", "A"): (1.0e-8, 100.0, "the area a force is spread over: a needle tip at 10^-8 m^2 to a foundation slab at 100 m^2"),
  ("p_abs=p_g+p_atm", "p_g"): (1000.0, 2.0e7, "a gauge pressure worth adding to 101 kPa: blood pressure at ~16 kPa to a 20 MPa gas cylinder"),
  ("p=p_0+rho*g*h", "h"): (0.01, 10000.0, "a depth in a liquid: a beaker at 1 cm to the 10,900 m Challenger Deep"),
  ("p=p_0+rho*g*h", "rho"): (600.0, 13600.0, "a liquid: gasoline 680, water 1000, seawater 1025, mercury 13,600"),
  ("m=rho*V", "rho"): (0.5, 22600.0, "a material density: a gas at ~1, water at 1000, osmium at 22,600 kg/m^3"),
  ("m=rho*V", "V"): (1.0e-6, 1000.0, "a volume from a cubic centimetre to a thousand cubic metres"),
  ("rho=((m)/(V))", "V"): (1.0e-6, 1000.0, "a volume from a cubic centimetre to a thousand cubic metres"),
  ("F_D=((1)/(2))*C*rho*A*(v)^(2)", "rho"): (1.0, 1030.0, "the fluid the body moves through: air at 1.2, seawater at 1030"),
  ("F_D=((1)/(2))*C*rho*A*(v)^(2)", "A"): (1.0e-4, 10.0, "a frontal area from a ball bearing to a truck"),
  ("F_D=((1)/(2))*C*rho*A*(v)^(2)", "v"): (0.1, 300.0, "the speed of the body through the fluid, up to terminal velocity in a dive"),
  ("a_CM=((m*g*sin(theta))/(m+(I_CM/(r)^(2))))", "I_CM"): (1.0e-7, 10000.0, "a rolling body's moment of inertia: a marble at 4e-7 to a heavy wheel at 1e4 kg m^2"),
  ("W=Q_h-Q_c", "Q_h"): (1.0, 1.0e7, "the heat an engine takes in per cycle, a laboratory demonstration's joules to a power plant's megajoules"),
  ("W=Q_h-Q_c", "Q_c"): (1.0, 1.0e7, "the heat an engine exhausts per cycle, on the same scale as Q_h"),
  ("Delta_U=Q-W", "Q"): (1.0, 1.0e6, "the heat added to a gas sample in a laboratory process"),
  ("Delta_U=Q-W", "W"): (1.0, 1.0e6, "the work done by that gas sample"),
  ("Q=Delta_E_int-W", "Delta_E_int"): (1.0, 1.0e6, "the internal-energy change of a gas sample in a laboratory process"),
  ("Q=Delta_E_int-W", "W"): (1.0, 1.0e6, "the work done by that gas sample"),
  ("Delta_S=((Q)/(T))", "Q"): (1.0, 1.0e6, "the heat transferred reversibly at fixed temperature"),
  ("Delta_S_tot=Delta_S_h+Delta_S_c", "Delta_S_h"): (0.001, 10000.0, "an entropy change of a laboratory system, mJ/K to kJ/K"),
  ("Delta_S_tot=Delta_S_h+Delta_S_c", "Delta_S_c"): (0.001, 10000.0, "an entropy change of the surroundings, mJ/K to kJ/K"),
  ("C_p=C_V+R", "C_V"): (12.5, 45.0, "an ideal gas at constant volume: 3R/2 = 12.5 monatomic, 7R/2 = 29.1 diatomic with vibration, higher for a large polyatomic"),
  ("V=I*R", "I"): (0.001, 100.0, "a circuit current from a milliamp signal to a 100 A starter cable"),
  ("V=I*R", "R"): (0.1, 1.0e6, "a resistor from a fraction of an ohm to a megohm"),
  ("R=V/I", "V"): (0.1, 1000.0, "a circuit voltage from a tenth of a volt to a 1 kV supply"),
  ("R=V/I", "I"): (0.001, 100.0, "a circuit current from a milliamp signal to a 100 A starter cable"),
  ("P=V*I", "V"): (0.1, 1000.0, "a circuit voltage from a tenth of a volt to a 1 kV supply"),
  ("P=V*I", "I"): (0.001, 100.0, "a circuit current from a milliamp signal to a 100 A starter cable"),
  ("P=(I)^(2)*R", "I"): (0.001, 100.0, "the current through the resistor, a milliamp signal to a 100 A load"),
  ("P=(I)^(2)*R", "R"): (0.1, 1.0e6, "a resistor from a fraction of an ohm to a megohm"),
  ("P=(((V)^(2))/(R))", "V"): (0.1, 1000.0, "the voltage across the resistor, a tenth of a volt to a 1 kV supply"),
  ("P=(((V)^(2))/(R))", "R"): (0.1, 1.0e6, "a resistor from a fraction of an ohm to a megohm"),
  ("R_eqv=R_1+R_2", "R_1"): (0.1, 1.0e6, "a resistor from a fraction of an ohm to a megohm"),
  ("R_eqv=R_1+R_2", "R_2"): (0.1, 1.0e6, "a resistor from a fraction of an ohm to a megohm"),
  ("R_eqv=((1)/(1/R_1+1/R_2))", "R_1"): (0.1, 1.0e6, "a resistor from a fraction of an ohm to a megohm"),
  ("R_eqv=((1)/(1/R_1+1/R_2))", "R_2"): (0.1, 1.0e6, "a resistor from a fraction of an ohm to a megohm"),
  ("I_1=I_2+I_3", "I_2"): (0.001, 100.0, "a branch current at a junction, a milliamp to 100 A"),
  ("I_1=I_2+I_3", "I_3"): (0.001, 100.0, "a branch current at a junction, a milliamp to 100 A"),
  ("I_S=((N_P)/(N_S))*I_P", "I_P"): (0.001, 100.0, "a transformer primary current, a milliamp to 100 A"),
  ("tau=R*C", "R"): (1.0, 1.0e7, "the resistance of an RC network, ohms to tens of megohms"),
  ("tau=R*C", "C"): (1.0e-12, 1.0e-4, "a capacitor from a picofarad trimmer to a 100 uF electrolytic"),
  ("C=((Q)/(V))", "V"): (0.1, 1000.0, "the potential difference across the capacitor, a tenth of a volt to a kilovolt"),
  ("U_E=((1)/(2))*C*(V)^(2)", "C"): (1.0e-12, 1.0e-4, "a capacitor from a picofarad trimmer to a 100 uF electrolytic"),
  ("U_E=((1)/(2))*C*(V)^(2)", "V"): (0.1, 1000.0, "the potential difference across the capacitor, a tenth of a volt to a kilovolt"),
  ("V=((U_E)/(q))", "U_E"): (1.0e-9, 1.0e-4, "the electrostatic potential energy of the laboratory charge this record also declares: nJ to 0.1 mJ"),
  ("epsilon=B*l*v", "l"): (0.01, 10.0, "the length of the moving conductor, a rod on laboratory rails to a 10 m bar"),
  ("epsilon=B*l*v", "v"): (0.1, 100.0, "the speed at which the rod is dragged along the rails"),
  ("v_d=E/B", "E"): (100.0, 1.0e6, "a laboratory electric field, below the ~3e6 V/m breakdown of air"),
  ("v_d=((I)/(n*q*A))", "I"): (0.001, 100.0, "a current in a laboratory wire, a milliamp to 100 A, the same window as every other current in this table"),
  ("F=q*v*B", "v"): (1.0, 1000.0, "the speed of the charged body, matched to the laboratory-scale charge declared on this record"),
  ("F=q*v*B*sin(theta)", "v"): (1.0, 1000.0, "the speed of the charged body, matched to the laboratory-scale charge declared on this record"),
  ("r=((m*v)/(q*B))", "v"): (1.0e5, 3.0e7, "the electron this record supplies m and q for: 1e5 m/s is thermal, 3e7 m/s is 0.1c (2.5 keV)"),
  ("I_0=((V_0)/(Z))", "V_0"): (0.1, 1000.0, "a peak AC source voltage; mains peak is 170 V"),
  ("I_0=((V_0)/(Z))", "Z"): (0.1, 1.0e6, "an AC circuit's impedance, a fraction of an ohm to a megohm"),
  ("I_rms=V_rms/Z", "V_rms"): (0.1, 1000.0, "an rms source voltage; mains is 120-240 V"),
  ("I_rms=V_rms/Z", "Z"): (0.1, 1.0e6, "an AC circuit's impedance, a fraction of an ohm to a megohm"),
  ("V_rms=((V_0)/(sqrt(2)))", "V_0"): (0.1, 1000.0, "a peak AC voltage; mains peak is 170 V"),
  ("I_rms=((I_0)/(sqrt(2)))", "I_0"): (0.001, 100.0, "a peak AC current, a milliamp to 100 A"),
  ("P_ave=I_rms*V_rms", "I_rms"): (0.001, 100.0, "an rms circuit current, a milliamp to 100 A"),
  ("P_ave=I_rms*V_rms", "V_rms"): (0.1, 1000.0, "an rms circuit voltage; mains is 120-240 V"),
  ("Q=((omega_0)/(Delta_omega))", "omega_0"): (100.0, 1.0e9, "an LC resonance from audio to UHF"),
  ("Q=((omega_0)/(Delta_omega))", "Delta_omega"): (1.0, 1.0e8, "the resonance bandwidth; the record's precondition already requires it below omega_0"),
  ("v=f*lambda", "f"): (0.1, 20000.0, "a mechanical-wave frequency, a 0.1 Hz ocean swell to the top of hearing"),
  ("v=f*lambda", "lambda"): (0.001, 1000.0, "a mechanical wavelength, a millimetre ripple to a kilometre ocean swell"),
  ("v=((lambda)/(T))", "lambda"): (0.001, 1000.0, "a mechanical wavelength, a millimetre ripple to a kilometre ocean swell"),
  ("v=((omega)/(k))", "omega"): (0.1, 1.0e6, "a wave's angular frequency, from an ocean swell to ultrasound"),
  ("v=((omega)/(k))", "k"): (0.001, 10000.0, "a wave number, 2*pi/lambda for wavelengths from a kilometre to a millimetre"),
  ("f_1=((v)/(4*L))", "v"): (250.0, 1100.0, "the speed of sound in the tube's gas: 259 m/s in CO2, 343 in air, 1007 in helium"),
  ("f_1=((v)/(4*L))", "L"): (0.05, 10.0, "a closed tube from a 5 cm resonance tube to a 10 m organ pipe"),
  ("L=((v_w)/(4*f_1))", "v_w"): (250.0, 1100.0, "the speed of sound in the tube's gas: 259 m/s in CO2, 343 in air, 1007 in helium"),
  ("L=((v_w)/(4*f_1))", "f_1"): (20.0, 20000.0, "an audible fundamental"),
  ("f_beat=|f_2-f_1|", "f_1"): (20.0, 20000.0, "an audible tone"),
  ("f_beat=|f_2-f_1|", "f_2"): (20.0, 20000.0, "an audible tone"),
  ("I=((P)/(A))", "P"): (1.0e-6, 1.0e5, "a source power from a microwatt emitter to a 100 kW transmitter"),
  ("I=((P)/(A))", "A"): (1.0e-4, 10000.0, "the area the power crosses, a square centimetre detector to a hectare"),
  ("I=((P)/(4*pi*(r)^(2)))", "P"): (1.0e-6, 1.0e5, "a source power from a microwatt emitter to a 100 kW transmitter"),
  ("I=((P)/(4*pi*(r)^(2)))", "r"): (0.01, 10000.0, "distance from the source, a centimetre to ten kilometres"),
  ("I_2=I_1*((((r_1)/(r_2))))^(2)", "I_1"): (1.0e-12, 10.0, "a sound intensity from the 1e-12 W/m^2 threshold of hearing to a painful 10 W/m^2"),
  ("I_2=I_1*((((r_1)/(r_2))))^(2)", "r_1"): (0.01, 10000.0, "distance from the source, a centimetre to ten kilometres"),
  ("I_2=I_1*((((r_1)/(r_2))))^(2)", "r_2"): (0.01, 10000.0, "distance from the source, a centimetre to ten kilometres"),
  ("I=((((Delta_p_max))^(2))/(2*rho*v))", "Delta_p_max"): (2.0e-5, 200.0, "a sound pressure amplitude: 2e-5 Pa is the threshold of hearing, 200 Pa is about 140 dB"),
  ("I=((((Delta_p_max))^(2))/(2*rho*v))", "rho"): (1.0, 8000.0, "the density of the medium carrying the sound: air 1.2, water 1000, steel 7900"),
  ("f_obs=f_s*sqrt(((1-((v)/(c)))/(1+((v)/(c)))))", "f_s"): (1.0e8, 1.0e16, "the source's emitted frequency, a radio carrier through an optical spectral line"),
  ("f=((R)/(2))", "R"): (0.02, 20.0, "a spherical mirror's radius of curvature, a 2 cm dental mirror to a 20 m telescope"),
  ("f=((d_i*d_o)/(d_o+d_i))", "d_o"): (0.01, 100.0, "an object distance on an optical bench, a centimetre to a hundred metres"),
  ("f=((d_i*d_o)/(d_o+d_i))", "d_i"): (0.01, 100.0, "an image distance on an optical bench, a centimetre to a hundred metres"),
  ("P=((1)/(f))", "f"): (0.005, 2.0, "a focal length: 5 mm for a microscope objective, 0.5 m for reading glasses, 2 m for a telescope objective"),
  ("h_i=(((n_2)/(n_1)))*h_o", "h_o"): (0.001, 1.0, "an object height at the refracting surface, a millimetre to a metre"),
  ("lambda_n=((lambda)/(n))", "lambda"): (1.0e-9, 1.0e-5, "an optical wavelength in vacuum, matching the window declared for lambda in Delta_y=x*lambda/d"),

  # --- thermal ---------------------------------------------------------------------------------
  ("Q=m*c*dT", "c"):                (100.0, 15000.0, "specific heat, lead to water"),
  # --- acoustics -------------------------------------------------------------------------------
  ("I=((((Delta_p_max))^(2))/(2*rho*v))", "v"): (100.0, 6000.0, "speed of sound in a real medium"),
}

def quantity_range(rec, var):
    """(lo, hi, integral, why) for a SAMPLED given, or None if nothing is DECLARED for it.

    A LOOKUP, NOT A CHAIN (A24). Four sources, each keyed on the thing it decides, none able to
    claim a variable belonging to another:
      1. a supplied constant is not a draw and is out of scope -- the electron mass is correct and
         would fail a "laboratory mass" window by 25 orders of magnitude;
      2. _SCALE, per (record, variable), for quantities living at a constant's scale (A23);
      3. _KIND, per (record, variable), for dimensionless quantities the unit cannot separate;
      4. _UNIT_RANGE, for units that fix the quantity on their own.
    Anything else returns None: UNCHECKED, counted and printed, never implied clean.
    """
    if _const_for(rec, var) is not None: return None
    f = rec.get("f")
    scale = _SCALE.get((f, var))
    if scale is not None:
        return (scale[0], scale[1], False, scale[2])
    kind = _KIND.get((f, var))
    if kind is not None:
        return _KIND_RANGE[kind]
    unit = ((rec.get("units") or {}).get(var) or "").strip()
    if unit == "K" and not var.startswith(("Delta", "delta")):
        return (1.0, 1.0e4, False, "an absolute temperature is positive")
    if unit == "s":
        return (1.0e-9, 1.0e6, False, "a duration is positive")
    return _UNIT_RANGE.get(unit)


# A22c. SOME PRECONDITIONS ARE RELATIONS BETWEEN GIVENS and no per-variable range can express them.
# Measured by the Step 0 adversary: lambda >= d on 45.13% of double-slit documents (no interference
# maximum exists, the derivation is void); W_f > W_in on 47.62% of friction documents (friction
# dissipates more than was supplied, and the answer states a NEGATIVE output work). Both produce
# arithmetic that is correct and a scenario that cannot happen.
#
# Stated as predicates over the drawn values, checked after sampling and redrawn on failure. The
# list is short and explicit; a record not in it has no cross-variable precondition CHECKED, which
# is not the same as having none.
_PRECONDITION = {
    "Delta_y=x*lambda/d":        (lambda v: v["lambda"] < v["d"],
                                  "an interference maximum needs lambda < d"),
    "W_out=W_in-W_f":            (lambda v: v["W_f"] < v["W_in"],
                                  "friction cannot dissipate more than was supplied"),
    "Eff_C=1-((T_c)/(T_h))":     (lambda v: v["T_c"] < v["T_h"],
                                  "the cold reservoir must be colder than the hot one"),
    # A27. Five more, each measured violated on 26-54% of its record's documents in Step 0 run 6.
    # Every one produces correct arithmetic and a scenario that cannot happen.
    "W=Q_h-Q_c":                 (lambda v: v["Q_c"] < v["Q_h"],
                                  "an engine cannot exhaust more heat than it takes in"),
    "x=l-l_0":                   (lambda v: v["l"] > v["l_0"],
                                  "an extension is positive; l < l_0 is a compression"),
    "f_beat=|f_2-f_1|":          (lambda v: abs(v["f_2"] - v["f_1"]) <= 20.0,
                                  "a beat is audible only below about 20 Hz"),
    "Q=((omega_0)/(Delta_omega))": (lambda v: v["Delta_omega"] < v["omega_0"],
                                  "a resonance needs a bandwidth narrower than its centre"),
    # A27b. K AND E_1 ARE NOT INDEPENDENT: K is the energy of level n, so K = n^2 * E_1 and
    # n = sqrt(K/E_1) must be a positive integer. Drawing them separately gives "a quantum number
    # of 2.83", which is not a small error -- it is a statement that quantisation does not hold.
    # A predicate cannot construct the pair, so most draws are rejected and those documents are
    # dropped; teaching nothing about this record is better than teaching that n is continuous.
    "n=((K/E_1))^(1/2)":         (lambda v: abs(math.sqrt(v["K"] / v["E_1"])
                                               - round(math.sqrt(v["K"] / v["E_1"]))) < 1e-6
                                            and 1 <= round(math.sqrt(v["K"] / v["E_1"])) <= 12,
                                  "n = sqrt(K/E_1) must be a positive integer: K = n^2 * E_1"),
    "a_CM=((m*g*sin(theta))/(m+(I_CM/(r)^(2))))":
                                 (lambda v: v["I_CM"] <= v["m"] * v["r"] ** 2,
                                  "no rigid body has more inertia than a hoop of its mass, radius"),
}

# A27c. SOME VARIABLES ARE NOT INDEPENDENT, AND A FILTER CANNOT MAKE THEM SO. `K = n^2 * E_1` ties
# two givens through an integer; drawing them separately and rejecting the mismatches left 4
# surviving documents in 20,000, so the record was effectively untaught -- and coverage of what the
# device can retrieve is the one property with a measured effect on correctness.
#
# A derivation CONSTRUCTS the dependent value instead. It runs after sampling and before the
# preconditions, so a record can use either or both. Keep these to genuine physical dependencies:
# anything expressible as a range belongs in _SCALE, and anything expressible as a test belongs in
# _PRECONDITION -- a derivation is the tool of last resort because it fixes a relationship the
# draw would otherwise have to stumble on.
def _derive_quantum_K(vals, rng):
    """K is the energy of level n in the same well: K = n^2 * E_1, n a positive integer."""
    n = rng.randint(1, 12)
    vals["K"] = vals["E_1"] * n * n
    return vals

_DERIVE = {
    "n=((K/E_1))^(1/2)": _derive_quantum_K,
}


def preconditions_hold(rec, vals):
    """True if the drawn values satisfy the record's cross-variable precondition, or it has none."""
    pred = _PRECONDITION.get(rec.get("f"))
    if pred is None: return True
    try:    return bool(pred[0](vals))
    except (KeyError, TypeError, ZeroDivisionError):
        return True            # the record does not have those variables; not this check's call


def sample_in_range(rng, lo, hi, integral):
    """Draw from the empirical pool, restricted to the range; fall back to a uniform draw when the
    pool offers nothing there, so a narrow window cannot silently empty the distribution."""
    # A28. THE POOL IS PREFERRED ONLY IF IT OFFERS REAL VARIETY. `if pool:` accepted a window
    # containing a SINGLE pool value, so seven declared givens were drawn as the same number in
    # 100% of documents -- q, Q, sigma and a plate gap were all always 0.001. A variable that never
    # varies is worse than one drawn too wide: the model sees a constant where the physics has a
    # free parameter, and no range check can see it because the value is in range every time.
    #
    # The mechanism is that a declared range can only FILTER the mined pool; it reaches values the
    # pool lacks only through the fallback below. So the fallback must fire not just when the pool
    # offers NOTHING, but whenever it offers too little to be a distribution.
    pool = sorted({v for v in _POOL if lo <= v <= hi and (not integral or float(v).is_integer())})
    if len(pool) >= 5: return rng.choice(pool)
    if integral:
        if pool: return rng.choice(pool)          # few but real integers: use them
        return float(rng.randint(int(lo), max(int(lo), int(hi))))
    # LOG-UNIFORM, AND SIGNIFICANT FIGURES RATHER THAN DECIMAL PLACES.
    #
    # `round(x, 4)` counts DECIMAL PLACES: round(3e-9, 4) is 0.0. It turned every optical
    # wavelength into ZERO -- the fallback that exists to reach small values was annihilating them,
    # and the range check then flagged its own sampler. The same shape as the significant-figures
    # bug in answer_matches_result, in the other direction.
    #
    # Log-uniform because these windows span orders of magnitude; a linear draw over [1e-9, 1e-5]
    # is 1e-5 with probability 0.9999.
    if lo <= 0: return float(f"{rng.uniform(lo, hi):.3g}")
    return float(f"{math.exp(rng.uniform(math.log(lo), math.log(hi))):.3g}")

VAR = re.compile(r"(?<![A-Za-z0-9_])([A-Za-z][A-Za-z0-9_]*)(?![A-Za-z0-9_(])")
RES = {"pi","e","sin","cos","tan","ln","log","sqrt","exp","d","f","x","t"}

def usable(r):
    """Physics-relation shape. The FIRST version of this filter passed Blv_d=55.0 and
    Dsintheta=mlambda -- fluent documents in which three variables had been collapsed into one
    identifier by the MathML converter. The diversity metrics scored that run as healthy
    (0.62 distinct 4-gram, 95% head coverage), because diversity cannot see semantic garbage.
    These two clauses are what catch it, and they are why a semantic gate exists separately."""
    f = r["f"]
    if "(" in f.split("=")[0]: return False               # f(x)= is a definition, not a relation
    if re.search(r"[A-Za-z_]\d*\s*\*?\(", f): return False  # function application, incl. f*(x)
    lhs, rhs = f.split("=", 1)
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", lhs): return False
    vs = {v for v in VAR.findall(rhs)} - {"pi","e"}
    # SEMANTIC GATE: the answer must require arithmetic, not just echo a substituted value,
    # and a relation with one variable teaches nothing about combining quantities.
    if not re.search(r"[+\-*/^]", rhs): return False
    # Prefix operators that the converter cannot distinguish from multiplication: Delta*V is
    # "change in V", not Delta times V; d*x/d*t is a derivative, not four multiplied symbols.
    # 7% of gated records. Dropped rather than shipped wrong -- the physics is silently altered
    # otherwise, and no downstream gate would catch it.
    if re.search(r"\b(Delta|d|partial|Sigma|nabla)\s*\*", f): return False
    return 2 <= len(vs) <= 4 and len(rhs) <= 40

# Only ANNOTATED, dimensionally-gated records enter the corpus. The prompt format signed off in
# PROMPT_FORMAT section 2 is FORMULA | VAR:UNIT ... | CONDITION, and the previous generator emitted
# FORMULA | VAR VAR -- no units, no condition. That made every eval prompt out-of-distribution and
# meant the applicability condition, which licenses every D-category refusal, never appeared in
# training at all.
# TRAINING formulas only. The held-out set is reserved strictly for measuring generalisation
# across unseen formulas and must never reach the corpus.
# ITERATE THE ANNOTATED SET, NOT THE MINED ONE.
#
# This used to read `for r in records_raw if usable(r) and r["f"] in units_train`, which iterates the
# MINED set and merely filters by the annotation store. records_raw holds 377 relations extracted
# from OpenStax "Key Equations" tables; units_train holds 200 hand-annotated with units. The
# intersection is 78, and the 122 relations that were annotated but never mined were unreachable --
# including FIFTEEN OF THE TWENTY the eval set tests. `v=d/t` carries 19 of the 200 eval items and
# had never appeared in a single training document. Measured: 5 of 20 eval heads covered, 21.6% of
# eval items. See docs/RELATION_COVERAGE.md.
#
# The reason mining cannot supply them is structural: Key Equations tables list what a chapter
# DERIVES, and the foundational relations -- F=m*a, v=d/t, K=0.5*m*v^2 -- are assumed and stated
# inline in prose. Re-running the miner over all twelve books yields 0 new relations. Breadth comes
# from mining; the core comes from hand-authoring, and iterating the mined set makes the core
# unreachable by construction.
_ann  = {r["f"]: r for r in json.load(open("corpus/units_train.json"))}
_mined = {r["f"]: r for r in json.load(open("corpus/records_raw.json"))}
# NAMES. units_train's `name` is often the WORKED EXAMPLE'S TITLE rather than the quantity's --
# "Curiosity Rover", "X-Rays from Aluminum", "(from worked example)" -- and generate.py puts that
# straight into the question as "find the {name}". store_clean.json was hand-curated with real
# relation names, so prefer it; 45 of the 82 bad names are covered that way, and all 20 eval
# relations get a proper name with no new work.
_store = {r["f"]: r for r in json.load(open("corpus/store_clean.json"))}


def lhs_unit(r):
    """The unit the ANSWER must carry, read from the store first for the reason A9 documents.

    A10. 81% of answer spans stated a dimensioned result with NO UNIT -- "The kinetic energy is 18."
    That is the model's training target, so it is a correctness defect in the shipped output, not a
    cosmetic one: a student is shown a bare number and every gate passed it because grade.py had no
    unit check and items.json's references are bare ("250", not "250 N/m").

    Dimensionless records render BARE, which is the same ruling as F3 in tools/eval/dispatch.c --
    "0.25", not "0.25 1", because that is what a physicist writes. 8 of 141 records are affected and
    they must not acquire a spurious "1"."""
    lhs = r["f"].split("=", 1)[0].strip()
    u = (_store.get(r.get("f"), {}) or {}).get("units") or r.get("units") or {}
    un = (u.get(lhs) or "").strip()
    return "" if un in ("", "1") else un


# A17. A PHYSICALLY IMPOSSIBLE RESULT IS NOT A TRAINING EXAMPLE, and nothing owned this.
# distribution_gate's tokenizer is `[a-z]+`, so it cannot see digits at all; provenance checks that
# a number was SOURCED, not that it is possible; the shape check compares structure; dim_gate
# checks dimensions, and -28.75 is dimensionally a fine efficiency. Measured 94 of 4,957 documents
# (1.90%): negative times of flight, a refractive index of 3.06e+07, a Carnot efficiency of -28.75,
# g = 4352 m/s^2.
#
# The mechanism is sample_value() drawing each variable independently, so nothing stops T_c > T_h
# or an angle past 90 degrees. Filtering the RESULT rather than constraining every input is the
# narrow fix: it needs one rule per quantity CLASS instead of per relation, and it cannot miss a
# combination nobody thought of.
#
# NARROW BY CONSTRUCTION, and the limit is the honest part: it covers quantity classes that have a
# NAMED physical range. A quantity outside this table is unchecked, not blessed -- most results are,
# and "not in the table" must not read as "verified plausible".
# A25. THE RESULT CHECK WAS THE SAME CHAIN, and it is replaced before it misdispatches rather
# than after. implausible() keyed its last three rules on the record NAME -- `"efficiency" in name`
# claims a window for the LHS of ANY record whose name contains that word, whatever the LHS
# actually is. Today each rule claims exactly one record and each LHS is the right quantity, so
# nothing is wrong; the mechanism is A24's exactly, and it will misfire the first time a record is
# added or renamed. A24 was found only because a regex happened to break; this one is being closed
# while it still looks fine.
#
# Declared per FORMULA (the result is the LHS, so the formula identifies it uniquely). The
# unit-determined rules stay: a value in seconds IS a duration, and that is the unit fixing the
# quantity, not a heuristic reading a name.
_RESULT_RANGE = {
    "efficiency":       (0.0, 1.0,   "an efficiency must lie in [0, 1]"),
    # ONE WINDOW PER QUANTITY. This said (1, 100) while _KIND_RANGE said (1, 4) -- the same
    # physical quantity with two windows 25x apart, so a result n = 40 passed while a GIVEN n = 40
    # would have failed. Matched to the given side.
    "refractive_index": (1.0, 4.0, "a refractive index must lie in [1, 4]"),
    # (0, 100) WAS TOO LOOSE TO FIRE. Measured over 89 g-documents the results spanned
    # [4.39e-06, 98.7] and every one passed -- a g of 4e-6 m/s^2 is not any body a textbook
    # mentions. Pluto is 0.62 and Jupiter 24.8, so [0.1, 30] is generous for the whole solar
    # system and still excludes the absurd. A range wide enough never to fire is not a check.
    "g_local":          (0.1, 30.0, "g must be that of some real body: Pluto 0.62, Jupiter 24.8"),
}
_RESULT_KIND = {
    "Eff_C=1-((T_c)/(T_h))":          "efficiency",
    "n=((c)/(v))":                    "refractive_index",
    "g=((4*(pi)^(2)*L)/((T)^(2)))":   "g_local",
}


def implausible(r, value):
    """Why this result cannot occur physically, or None.

    A LOOKUP PLUS SOUND UNIT RULES (A25), not a chain of name matches. A record with no declared
    result kind and no decisive unit is UNCHECKED -- most are, and gate_plausible prints the
    fraction so that is visible rather than implied.
    """
    lhs = r["f"].split("=", 1)[0].strip()
    unit = ((r.get("units") or {}).get(lhs) or "").strip()
    # The unit fixes the quantity here -- no inference.
    if unit == "s" and value <= 0:  return "a duration must be positive"
    if unit == "kg" and value <= 0: return "a mass must be positive"
    if unit == "K" and not lhs.startswith(("Delta", "delta")) and value <= 0:
        return "an absolute temperature must be positive"
    kind = _RESULT_KIND.get(r.get("f"))
    if kind is not None:
        lo, hi, why = _RESULT_RANGE[kind]
        if not lo <= value <= hi: return why
    return None


def _num(x):
    """Render a given so the tool call's literal is byte-findable in the question.

    `{:g}` is a DISPLAY format: it rounds 1054.804 to "1054.8", and the call then carries the full
    float, so provenance correctly reports a literal the model never saw. repr() round-trips, and
    trailing ".0" is trimmed because "m = 2.0" reads wrong where "m = 2" reads right -- and 2.0 and
    2 are the same float, so the call still matches."""
    r = repr(float(x))
    return r[:-2] if r.endswith(".0") else r


def _condition_field(r):
    """The record span's condition field, by the SHIPPED assembler's rule (assemble.c:94).

    Reads `req` from the STORE, not from the annotated record the generator iterates -- 0 of 141
    of those carry it, which is exactly how the two producers came to disagree on 57.4% of records
    while each looked internally consistent."""
    req = (_store.get(r.get("f"), {}) or {}).get("req") or r.get("req")
    return req if req else "standard conditions"
# Rejects a WORKED-EXAMPLE TITLE used as a relation name ("Using the Monotone Convergence Theorem",
# "Calculating Displacement: A Subway Train"). Every marker names a thing a TITLE does.
#
# Bare `Theorem` was in this list and is not such a marker: it rejected "Work-energy theorem" and
# "impulse-momentum theorem", two legitimate relation names, leaving both records shipped-but-
# untrained. "Contains the word Theorem" was a proxy for "is a worked-example title", and the
# titles it was meant to catch all carry a leading verb -- `Using ` catches the Monotone
# Convergence one on its own.
#
# A name that denotes a RELATION rather than a quantity is handled downstream, not here:
# quantity_surface() sends those to the LHS symbol (A6f), so "Work-energy theorem" produces
# "Calculate W_net", never "Calculate work-energy theorem".
_BADNAME = re.compile(r"^\(|Example|Using |Calculat|^Find |Determin|Problem", re.I)

def _best_name(f, ann):
    for cand in (_store.get(f, {}).get("name"), ann.get("name"), _mined.get(f, {}).get("name")):
        if cand and not _BADNAME.search(cand): return cand
    return None

# R_train SUPSETEQ R_store. THE ITERATION SET IS THE UNION, and it used to be units_train alone --
# so 25 records the device can RETRIEVE were never taught. A record the model has never seen
# produces a well-formed document with the wrong structure at 12.2% against 41.0% for one it has,
# and the picker can return any of the 166. Shipping a relation you did not train on is shipping a
# confident wrong answer with a retrieval path to it.
#
# All 25 carried COMPLETE units and a name in store_clean.json already; nothing had to be annotated,
# the iteration simply never reached them. units_train stays the authority on units WHERE IT HAS
# THEM -- it is the hand-annotated set -- and the store supplies them otherwise.
#
# This closes the rule and stops. Breadth beyond the store (the 377 Gate-A survivors) is a separate
# decision with its own control: it has no measured effect on correctness, and coverage of what
# SHIPS is what 41.0/12.2 measures.
recs, _unnamed, _uncleaned = [], [], []
_keys = list(_ann) + [f for f in _store if f not in _ann]
for f in _keys:
    ann = _ann.get(f) or {}
    src = _mined.get(f) or _store.get(f) or dict(ann)
    r = dict(src); r["f"] = f
    r["units"] = ann.get("units") or (_store.get(f, {}) or {}).get("units") or {}
    if not r["units"]:
        _unnamed.append(f)                     # no units anywhere: cannot state an answer's unit
        continue
    # usable() IS A MINING FILTER. Its two clauses catch converter artefacts -- `f(x)=` definition
    # shapes and MathML fusion like `K*E` where `KE` was meant -- which can only occur in text that
    # came through extract_records.py. Applying it to a HAND-CURATED record is a category error, and
    # an expensive one: it dropped 23 hand-verified store records including E=m*c^2, kinetic energy,
    # drag force, the law of reflection and rms voltage. Its function-application regex,
    # `[A-Za-z_]\d*\s*\*?\(`, matches `m*(v)^(2)` -- an ordinary product -- and `K=0.5*m*(v)^(2)`
    # carries 12 of the 200 eval items.
    #
    # So: a record that appears in store_clean.json has been read by a human and is exempt. A
    # mined-only record is not, and the filter still earns its place there -- of the 4 mined-only
    # drops, one is `K*E=...` (fusion) and one is `U(x)=...` (definition shape).
    if r.get("drop"): continue
    # store_clean.json IS THE AUTHORITY ON LEGITIMACY. It is the OUTPUT of a review that deleted 34
    # records for documented reasons (docs/RESULT_STORE_CLEANING.md): non-physics recurrences,
    # duplicates, and misnamed relations -- `R_eqv=R_1-R_2` (resistances do not subtract),
    # `v=lambda*f` labelled "speed of light" when it is wave speed, `F=k*x` against the signed
    # `F=-k*x`. units_train.json is the authority on UNITS and was never re-cleaned, so it still
    # carries all three.
    #
    # The first version of this change iterated units_train alone and RE-ADMITTED 24 of them,
    # including three named "Strategy", one named "Graficar una ecuacion polar", and two whose name
    # is their own formula. That is the store-cleaning silently undone by a change made two
    # commits later -- the same shape as every other defect in this file's history, and caught only
    # by auditing what had actually landed.
    if f not in _store:
        _uncleaned.append((f, ann.get("name", "")))
        continue
    if not usable(r) and f not in _store: continue
    nm = _best_name(f, ann)
    if not nm:
        # REPORTED, NOT SILENT. usable() already drops 31 records with no output at all, and simple
        # harmonic motion is absent from the corpus because of it. A relation skipped for want of a
        # name is a relation someone can supply a name for in one line, and they can only do that if
        # the generator says which.
        _unnamed.append(f); continue
    r["name"] = nm
    recs.append(r)

# Applicability conditions, drafted per subject. THIN by design and flagged as such: these are the
# weakest annotation in the pipeline and the one the refusal metrics lean on hardest.
COND = [
 (r"roll|rotat|angular|torque|moment of inertia", "rigid body, fixed axis"),
 (r"mirror|lens|optic|focal|magnif|refract",      "thin lens or spherical mirror, paraxial rays"),
 (r"drag|fluid|buoyan|viscos|flow",               "steady flow, constant density"),
 (r"circuit|capacit|induct|resist|current|ohm",   "steady state, ohmic components"),
 (r"photon|quantum|debroglie|planck|bohr",        "non-relativistic, single particle"),
 (r"therm|heat|entrop|gas|carnot|molar",          "quasi-static, no phase change"),
 (r"spring|hooke|oscillat|harmonic",              "within the elastic limit, no damping"),
 (r"magnetic|solenoid|hall|lorentz|flux",         "uniform field, steady current"),
 (r"free fall|projectile|kinemat|velocity|accel", "constant acceleration, no air resistance"),
 (r"relativ|doppler|lorentz factor",              "inertial frames, constant relative velocity"),
]
def condition(name):
    n = name.lower()
    for pat, c in COND:
        if re.search(pat, n): return c
    return "standard conditions"

_NOT_A_HEAD = {"of", "in", "on", "for", "to", "a", "an", "the", "and", "or", "with",
               "from", "by", "at", "per", "into", "under", "over", "as", "its"}

# A NAME THAT DENOTES A RELATION IS NOT A NAME FOR ITS OUTPUT. 24 of 141 records are named after
# the equation rather than the quantity it produces -- "law of reflection", "transformer equation",
# "common expression of ohm's law", "relationship between frequency and period" -- and asking
# "Compute law of reflection." is asking for the wrong kind of thing. For these the only honest
# surface is the LHS symbol. Found by hand-reading 30 questions after the frame fix: 4 of 30 were
# of exactly this shape and nothing else explained them.
# A14. Extended: a name denoting a PROCESS, FUNCTION or EFFECT is no more a name for a quantity
# than a law is. "energy-mass conversion", "Potential-energy function of a harmonic oscillator" and
# "Relativistic Doppler effect for frequency" all produced asks for the wrong kind of thing --
# 128 of 11,975 documents. Same rule as the law/theorem case: send them to the LHS symbol.
_RELATION_NAME = re.compile(r"\b(law|equation|expression|relationship|relation|theorem|"
                            r"principle|rule|formula|identity|definition|"
                            r"conversion|function|effect|process)\b", re.I)

# A15. A BARE PROPERTY NOUN IS NOT A QUANTITY. Head extraction turned "Index of refraction" into
# "index", "Magnitude of magnetic force" into "magnitude" and "change in Fahrenheit temperature"
# into "change", giving "Find index." and "Determine magnitude." -- 191 of 11,975 documents asking
# for nothing identifiable. These words are the PROPERTY, and the thing they are a property OF is
# what got dropped. When the head reduces to one of them, keep the full name instead.
# A26. THE LAST INFERENCE ON A NAME, converted before it misdispatches (A24's lesson applied
# rather than re-learned). _RELATION_NAME is a regex over the record's name deciding whether that
# name denotes a RELATION rather than a quantity -- "Law of reflection" must not become "Calculate
# law of reflection." It currently classifies all 18 correctly, and it is exactly the shape that
# failed in quantity_range: a pattern standing in for a fact about a record.
#
# So the fact is declared. The regex is KEPT, but only as a DISCOVERY AID: the assertion below
# fails if a record's name matches it and is not declared, so a newly added "…law" record is
# noticed rather than silently classified. Declaration decides; the pattern only nags.
_NAME_IS_RELATION = {
    'Delta_S=((Q)/(T))',
    'Delta_U=Q-W',
    'Delta_p=F_net*Delta_t',
    'E=m*(c)^(2)',
    'F=-k*x',
    'F=G*m1*m2/(r)^(2)',
    'F=m*a',
    'F_net=m*a',
    'I_0=((V_0)/(Z))',
    'I_1=I_2+I_3',
    'I_S=((N_P)/(N_S))*I_P',
    'U=((1)/(2))*m*(omega)^(2)*(x)^(2)',
    'V=((U_E)/(q))',
    'V=I*R',
    'W_net=K_B-K_A',
    'f=((1)/(T))',
    'f=((d_i*d_o)/(d_o+d_i))',
    'f_obs=f_s*sqrt(((1-((v)/(c)))/(1+((v)/(c)))))',
    'p=((h)/(lambda))',
    'theta_2=asin(((n_1*sin(theta_1))/(n_2)))',
    'theta_r=theta_i',
    'w=m*g',
}


_NOT_A_QUANTITY = {"magnitude", "index", "change", "difference", "value", "component",
                   "ratio", "number", "amount", "factor", "rate", "size", "amount"}

def rec_is_relation_named(r):
    """Does this record's NAME denote a relation rather than the quantity it computes?

    DECLARED (A26), not matched. One lookup, so the question surface and the answer template cannot
    disagree about it -- they consulted the regex separately before, which is the two-implementations
    pattern waiting to happen."""
    return r.get("f") in _NAME_IS_RELATION


def quantity_surface(r, rng):
    """How the QUESTION refers to the quantity being asked for.

    A1. This was `r["name"].lower()` -- the record's canonical name, interpolated verbatim into
    every question, so a training question named its own record and an eval question never did.
    docs/UNENFORCED_REVIEW.md A1.

    IT CANNOT SIMPLY BE REMOVED. "Find the ___" is unanswerable; a question has to say what it
    wants. What was wrong was using ONE surface form, always, and that form being the exact string
    the retrieval record carries. So: vary it, from material the record already has.

    Four surfaces, and none is invented physics:
      the canonical name            "the gravitational potential energy"
      the head noun                 "the potential energy"   (last two words)
      the bare head                 "the energy"
      the symbol                    "U"
    The last two are what a textbook actually writes once the quantity is in play, and they are what
    the eval set writes. Weighted toward the shorter forms because the corpus was 100% canonical.

    MEASURED, and stated because it is small: this is worth ~2 pp of the separability that
    distribution_gate reports, not the 34 pp the gate is above its floor. See RESULT_SEPARABILITY.md
    -- the dominant cause is that the generator's ENTIRE question vocabulary is 249 words."""
    name = (r.get("name") or "").lower().strip()
    lhs  = r["f"].split("=", 1)[0].strip()
    words = name.split()
    forms, weights = [], []

    def _add(form, w):
        """A truncation is only a noun phrase if it BEGINS like one.

        The first version of this took the last two words unconditionally, which turned
        "index of refraction" into "of refraction" and produced "Calculate of refraction."
        A suffix of a noun phrase is not itself a noun phrase, and taking one is a proxy for
        head-noun extraction rather than the thing itself."""
        form = form.strip()
        if not form or form.split()[0] in _NOT_A_HEAD:
            return
        if form.lower() in _NOT_A_QUANTITY:      # A15: names the property, not the quantity
            return
        forms.append(form); weights.append(w)

    if name and rec_is_relation_named(r):
        # The name describes the equation, not its output. Only the symbol is usable.
        return lhs or name
    if name:
        _add(name, 2)
        # THE HEAD IS ON THE LEFT OF A PREPOSITION, NOT AT THE END. "acceleration of two blocks
        # connected over a pulley" has head "acceleration"; the last word is "pulley", and taking
        # it produced "what was pulley?". Likewise "speed of sound" -> "sound", "distance to
        # screen" -> "screen", "heat capacity at constant pressure" -> "constant pressure".
        # Position was a proxy for headedness and it is wrong for every "X of Y" name, which is
        # most of them.
        head = re.split(r"\s+(?:of|from|in|on|at|for|to|over|due|between|with|per)\s+", name)[0]
        if head and head != name: _add(head, 3)
    if lhs and len(lhs) <= 6:
        _add(lhs, 2)
    if not forms:
        return name or lhs or "the value"
    return rng.choices(forms, weights=weights, k=1)[0]


def units_field(r):
    """The record span's units field, for ONE record. Exported because the parity gate calls it.

    An earlier version of tools/eval/gate_format_parity.py RE-IMPLEMENTED this rule instead of
    calling it, so mutating the generator did not move the gate and the negative control passed
    while the fix was reverted. A gate that carries its own copy of the rule tests the copy. Same
    lesson as tools/eval/genloop.py: when two places need one behaviour, the second one must call
    the first.

    THE LHS UNIT IS PART OF THE SPAN. `vs` comes from the RIGHT-hand side only, so the variable being
    solved for never got a unit here -- 0 of 197,428 shipped training documents carried it, while
    src/store/assemble.c has emitted it on EVERY device prompt since it was written ("units for
    every variable, LHS included -- the answer needs a unit to state") and test_assemble.c asserts
    it. So every prompt the device produced differed from every document the model trained on, in
    the first field after the formula. docs/EXPERIMENT_PLAN.md:295-313 records this exact skew as
    found and fixed -- "the five-field skeleton was unified". The skeleton was; the units were not.
    """
    u = r.get("units") or {}
    lhs = r["f"].split("=", 1)[0].strip()
    # FIRST-APPEARANCE ORDER, NOT SORTED. src/store/assemble.c walks r->var[] in declaration order,
    # which store_pack.py writes as the LHS then the RHS variables in the order they occur in the
    # formula. Sorting produced `v:m/s T:s lambda:m` where the device emits `v:m/s lambda:m T:s` --
    # a difference on 53.6% of records that gate_format_parity compared as a SET and excused as
    # cosmetic. It is not cosmetic: it is a token sequence the model never sees at inference.
    seen, vs = set(), []
    for v in VAR.findall(r["f"].split("=", 1)[1]):
        if v in ("pi", "e") or v in seen: continue
        seen.add(v); vs.append(v)
    order = ([lhs] if lhs in u else []) + [v for v in vs if v != lhs]
    return " ".join(f"{v}:{u[v]}" for v in order if v in u)

for r in recs: r["cond"] = condition(r["name"])

# A DECLARATION THAT MATCHES NOTHING IS A NO-OP WEARING A CLAIM'S CLOTHES. _SCALE shipped with two
# such keys -- my formula spellings, not the store's -- and they read as coverage. Checked BY PAIR,
# not merely by formula: a key naming a variable the record does not have is the same defect one
# level down.
_PAIRS = {(r["f"], v) for r in recs
          for v in ({x for x in VAR.findall(r["f"].split("=", 1)[1])} - {"pi", "e"})}
_DECL_STALE = sorted(f"{f}  [{v}]" for (f, v) in (set(_SCALE) | set(_KIND)) if (f, v) not in _PAIRS)
assert not _DECL_STALE, (
    "A23/A24: these declarations match no sampled (record, variable) pair, so they are silent "
    "no-ops:\n  " + "\n  ".join(_DECL_STALE)
    + "\nFix them against corpus/store_clean.json. A range that never fires reads as coverage.")
_DERIVE_STALE = sorted(set(_DERIVE) - {r["f"] for r in recs})
assert not _DERIVE_STALE, f"A27c: _DERIVE names records that do not exist: {_DERIVE_STALE}"
_RESULT_STALE = sorted(set(_RESULT_KIND) - {r["f"] for r in recs})
assert not _RESULT_STALE, f"A25: _RESULT_KIND names records that do not exist: {_RESULT_STALE}"
_RESULT_BAD = sorted(k for k in _RESULT_KIND.values() if k not in _RESULT_RANGE)
assert not _RESULT_BAD, f"A25: _RESULT_KIND names kinds with no range: {_RESULT_BAD}"
# The regex is a DISCOVERY AID, not the decision: a record whose name matches it and is not
# declared is a new record nobody classified, and that must be noticed rather than guessed at.
_UNDECLARED_RELATION = sorted(r["f"] for r in recs
                              if _RELATION_NAME.search(r.get("name") or "")
                              and r["f"] not in _NAME_IS_RELATION)
assert not _UNDECLARED_RELATION, (
    "A26: these records have a relation-sounding NAME and no entry in _NAME_IS_RELATION:\n  "
    + "\n  ".join(_UNDECLARED_RELATION)
    + "\nDecide for each whether the name denotes the quantity it computes, and declare it.")
_RELATION_STALE = sorted(_NAME_IS_RELATION - {r["f"] for r in recs})
assert not _RELATION_STALE, f"A26: _NAME_IS_RELATION names records that do not exist: {_RELATION_STALE}"
# A26b. NO SAMPLED VARIABLE MAY TAKE ITS VALUE FROM THE GLOBAL NAME TABLE. `CONST[v]` maps a bare
# identifier to a physical constant with no record scope -- the pattern IDENTIFIER_COLLISION.md
# records, and the last value-supplying inference in this file. It is dimensionally gated, which
# makes it fairly safe, and "fairly safe" is what A24 was too. Every constant a RHS variable needs
# is now declared in the store's `cval` or in _MICRO_CVAL; this fails if that stops being true.
_NAME_TABLE_USERS = sorted(
    f"{r['f']}  [{v}]" for r in recs
    for v in ({x for x in VAR.findall(r["f"].split("=", 1)[1])} - {"pi", "e"})
    if _const_for(r, v) is not None
    and (r["f"], v) not in _MICRO_CVAL
    and v not in ((_store.get(r["f"], {}) or {}).get("cval") or {}))
assert not _NAME_TABLE_USERS, (
    "A26b: these SAMPLED variables get their value from the global CONST name table rather than "
    "from a declaration:\n  " + "\n  ".join(_NAME_TABLE_USERS)
    + "\nDeclare them in _MICRO_CVAL or in the store's cval.")
_KIND_BAD = sorted(k for k in _KIND.values() if k not in _KIND_RANGE)
assert not _KIND_BAD, f"A24: _KIND names kinds with no range: {_KIND_BAD}"

print(f"records usable as physics relations: {len(recs)} of {len(_ann)} annotated "
      f"({len(_unnamed)} skipped for want of a relation name, "
      f"{len(_ann)-len(recs)-len(_unnamed)} by usable(), which exempts hand-curated records)")
if _uncleaned:
    print(f"  SKIPPED, NOT IN THE CLEANED STORE ({len(_uncleaned)}) -- units_train was never re-cleaned")
    print( "  after docs/RESULT_STORE_CLEANING.md deleted 34 records. Each needs a decision:")
    for _f, _n in _uncleaned[:10]: print(f"    {_f:34} {_n[:40]}")
    if len(_uncleaned) > 10: print(f"    ... and {len(_uncleaned)-10} more")
if _unnamed:
    print("  SKIPPED FOR WANT OF A NAME -- add one to corpus/store_clean.json to enable:")
    for _f in _unnamed[:12]: print(f"    {_f}")
    if len(_unnamed) > 12: print(f"    ... and {len(_unnamed)-12} more")

# ---- phrasing templates. Style varies, facts do not. -------------------------
# ASK templates come from the DEVELOPMENT population -- real OpenStax question openings, mined in
# corpus/stems.py. The eval set is NEVER used to select templates: it is the only held-out phrasing
# distribution we have, and tuning against it would destroy the honest read on whether variety
# closes the 25pp generator-vs-eval gap.

# A6. THE MINED ASK TEMPLATES ARE 8 USABLE FRAMES AND 36 FRAGMENTS, and {q} was substituted into
# all 44 blind. Two defects, one cause, both measured on output rather than argued:
#
#   1. WRONG QUANTITY. 16 templates name a quantity of their own, so the question asked for one
#      thing and the answer span supplied another. 4,507 of 20,000 shipped questions -- 22.5%.
#          "How much heat did velocity?"              record v=v_0+a*t
#          "How much work does tangential speed?"     record v=r*omega
#      That is wrong supervision: a quarter of the corpus taught that the quantity named in the
#      question is not the one to answer with.
#
#   2. WRONG FRAME. 36 of 44 templates are sentence fragments mined mid-question, whose complement
#      must be something other than a bare noun phrase. Hand-read of 30 generated questions:
#      19 ill-posed, 63% [Wilson 95% CI 46-78].
#          "Determine whether v_t."          needs a proposition
#          "How long will it take centripetal acceleration?"   needs a duration
#          "Calculate be/ F_net."            mining garbage, literally "be/"
#          "What is his displacement vector d -> k?"           mining garbage
#          "Determine (a) mass."             "(a)" leaked from multi-part exercise numbering
#
# WHAT MISSED BOTH. Every automated check passed these documents -- well-formed, call executes,
# result matches, provenance clean, shape OK, dimensional gate clean, diversity fine. Nothing
# compared the question's subject to the answer's, and nothing read a question. They were found by
# READING ONE, then thirty.
#
# THE FIX IS A HAND-REVIEWED ALLOWLIST, NOT A REGEX. A predicate like "does the template end in
# {q}?" would be a proxy for "does this frame accept a bare noun phrase", and this repo has been
# bitten six times by exactly that substitution. 44 items is small enough to read, so they were
# read, and the eight that survive are named here with the reviewer's claim attached.
#
# THE UNCOMFORTABLE PART, and it belongs in the plan rather than in a comment: mining question
# surfaces yielded EIGHT generic verbs that could have been hand-written in a minute. The template
# "variety" the corpus was credited with was 36 broken frames. This is independent evidence for the
# same conclusion the stem work reached -- mined OpenStax question surface does not transfer.
ASK_REVIEWED = [                      # every frame below takes a bare noun phrase as its object
    "What is {q}?", "Find {q}.", "Calculate {q}.", "Determine {q}.",
    "What was {q}?", "Estimate {q}.", "Compute {q}.",
    # "What are {q}?" is REJECTED despite being a clean frame: quantity_surface() yields singular
    # noun phrases, so it produced "what are spherical mirror?" and "what are motionally induced
    # emf?". A frame is only usable with the surfaces this generator actually emits.
]
_ASK_SHA = "2d0d3b8f"                 # guard: see tools/eval/gate_ask_quantity.py

ASK_ALL = json.load(open("corpus/asks_dev.json"))
ASK = [t for t in ASK_REVIEWED if t in ASK_ALL]
assert len(ASK) == len(ASK_REVIEWED), (
    "a reviewed ASK frame is no longer in corpus/asks_dev.json. The allowlist is a claim about "
    "text that has been READ; it cannot survive that text changing underneath it. Re-read the "
    "file and update ASK_REVIEWED deliberately.")

QUANTITY = re.compile(r"\b(work|heat|speed|velocity|displacement|force|power|energy|time|charge|"
                      r"current|mass|distance|pressure|temperature|acceleration|momentum)\b", re.I)

def _named_quantities(text):
    return {w.lower() for w in QUANTITY.findall(text or "")}

# Retained so the gate can assert the REJECTED frames stay rejected: a gate that only checks the
# survivors cannot notice the fragments coming back.
ASK_REJECTED = [t for t in ASK_ALL if t not in ASK_REVIEWED]

def ask_for(r, surface, rng):
    """Choose a question frame. Only reviewed frames are eligible, so a frame cannot contradict
    the quantity being asked for -- none of them names a quantity at all."""
    return rng.choice(ASK).format(q=surface)

GIVE = ["Given {g}, ", "With {g}, ", "If {g}, ", "For {g}, ", "Where {g}, ",
        "Suppose {g}. ", "Take {g}. ", "A system has {g}. ", "Assume {g}. ",
        "Consider a case where {g}. ", "In a setup with {g}, ", "Measurements give {g}. ",
        "You are told {g}. ", "The values are {g}. ", "Starting from {g}, ", "Using {g}, "]
# A16. `The {q} is ...` interpolates the record's NAME as the computed quantity, so a record named
# for a law produced "The law of reflection is 0.5 rad." -- 232 of 11,975 answers naming a law,
# effect or function as though it were the value. quantity_surface() already refuses those for the
# QUESTION (A6f/A14); the answer side had no such guard, which is the two-implementations pattern
# again. CLOSE_Q is used only when the record's name really denotes a quantity.
CLOSE_ANY = ["{v} = {a}. {why}", "{a}. {why}", "That gives {a}. {why}"]
CLOSE_Q   = CLOSE_ANY + ["The {q} is {a}. {why}"]
WHY = ["Substituting into {f}.", "Directly from {f}.", "From {f}.", "Using {f}.",
       "This follows from {f}.", "{f} gives it."]

def gen(n, seed=0):
    rng = random.Random(seed)
    docs, calls = [], []
    for i in range(n):
        r = rng.choice(recs)
        lhs, rhs = r["f"].split("=", 1)
        vs = sorted({v for v in VAR.findall(rhs)} - {"pi", "e"})
        def _draw(v):
            c = _const_for(r, v)
            if c is not None: return c
            rr = quantity_range(r, v)
            return sample_in_range(rng, rr[0], rr[1], rr[2]) if rr else sample_value(rng)
        vals = {v: _draw(v) for v in vs}
        _der = _DERIVE.get(r.get("f"))
        if _der is not None: vals = _der(vals, rng)
        for _try in range(24):                     # bounded: a precondition that can never hold
            if preconditions_hold(r, vals): break  # must not spin, and the doc is dropped below
            vals = {v: _draw(v) for v in vs}
            if _der is not None: vals = _der(vals, rng)
        if not preconditions_hold(r, vals):
            continue                               # cannot satisfy it -- drop, never teach it
        expr = VAR.sub(lambda m: f"({vals[m.group(1)]})" if m.group(1) in vals else m.group(1), rhs)
        free = [v for v in vs if _const_for(r, v) is None]
        if not free: continue                          # nothing left to ask about

        # A7. CONSTANTS ARE GIVENS, and leaving them out taught the model to fabricate numbers.
        #
        # 27 of 141 records carry a resolved constant (g, c, h, G, k_e, R, k_B, eps_0, mu_0). The
        # value went into the TOOL CALL and into neither the question nor the five-field record, so
        # 16.2% of documents (measured, 4,000-doc sample) emit a literal that appears NOWHERE in the
        # model's context. That is the architecture inverted: TOOL_SPEC exists so the model reads
        # numbers rather than recalling them, and a sixth of the corpus trained it to recall one.
        #
        # AND IT MADE THE DEFECT UNGRADEABLE, which is why it survived. Measured on grade.py:
        #
        #   constant absent (as shipped)  g=9.81  -> prov False, shape UNCHECKED
        #   constant absent (as shipped)  g=9810  -> prov False, shape UNCHECKED   <- identical
        #   constant inlined              g=9.81  -> prov True,  shape ok
        #   constant inlined              g=9810  -> prov False, shape MISMATCH
        #
        # Correct and fabricated were indistinguishable to every grader in the repo. grade.py's own
        # docstring says the shape check exists for this case -- "the mgh case proves it" -- and it
        # degraded to `unchecked` on exactly the mgh case. Same shape as dim_gate returning
        # "unknown": "cannot check" scored as "nothing wrong".
        #
        # docs/PROMPT_FORMAT.md section 2 already mandates inlining, build/asmcli already does it on
        # the device path, and tools/eval/items.json A1-004 does it. The generator did not, so the
        # corpus disagreed with the runtime that consumes it.
        consts = [v for v in vs if _const_for(r, v) is not None]
        shown  = free + consts
        # A13. FULL PRECISION IN THE GIVENS. `{:g}` renders 1054.804 as "1054.8" while the call
        # uses the full float, so the literal in <arg> appeared nowhere in the model's context --
        # the A7 defect again, 2 of 10,266 calls, arriving through DISPLAY ROUNDING rather than
        # through an omitted constant. repr() round-trips.
        g = ", ".join(f"{v} = {_num(vals[v])}" for v in shown)
        # Decide AFTER the question exists, not before. Three refusal kinds from this path:
        #   withhold -> a required given is absent   (D1)  missing:X | ... | fit:high
        #   mismatch -> the record does not fit      (D2)  missing:none | ... | fit:high
        #   nomatch  -> the picker found nothing     (D3)  none | missing:none | ... | fit:low
        #
        # A11. THE FIT LABELS NOW MATCH THE DEVICE, AND D2's DID NOT.
        #
        # src/store/assemble.c has exactly two shapes and no others:
        #     line  99   a real record   -> ALWAYS " | fit:high"
        #     line 109   no match        -> "none | missing:none | no matching relation | fit:low"
        #
        # D2 emitted a REAL RECORD with fit:low -- a shape the runtime can never produce. 616 of
        # 11,975 documents (5.14%) were in it, and ZERO were in the shape the device actually emits
        # when the picker finds nothing. So the corpus taught a prompt the model will never see and
        # never taught the one it will.
        #
        # And the fix is not cosmetic. On the device a BAD match still arrives as fit:high with a
        # real record, because ns_assemble does not judge fit -- the picker just returns its best
        # candidate. So D2 must be fit:high: the model has to refuse from the record's CONTENT not
        # matching the question, which is the judgement, rather than from a label. That is the A8
        # lesson one level deeper -- there the leak was a formatting bit, here it was the fit token
        # itself, and a model trained on the old labels could refuse correctly on every D2 case
        # while having learned nothing about fit.
        #
        # gate_format_parity could not see this: it compares the generator's record span against
        # asmcli FOR A GIVEN RECORD, and the no-match case has no record to compare.
        roll = rng.random()
        withhold = roll < 0.10 and len(vs) >= 2
        mismatch = 0.10 <= roll < 0.15
        nomatch  = 0.15 <= roll < 0.18
        if withhold:
            # A12. WITHHOLD A FREE VARIABLE, NEVER A CONSTANT. `rng.choice(vs)` could pick g, c, h
            # or G -- values the device ALWAYS inlines (A7) -- so 111 of 11,975 documents refused
            # for want of a number the runtime would have supplied. A refusal that fires on a
            # satisfied precondition is wrong supervision: it teaches the model to refuse a
            # question it can answer.
            drop = rng.choice(free) if free else rng.choice(vs)
            shown_w = [v for v in shown if v != drop]
            g = ", ".join(f"{v} = {_num(vals[v])}" for v in shown_w) if shown_w else g
        stem = rng.choice(GIVE).format(g=g)
        ask  = ask_for(r, quantity_surface(r, rng), rng)
        # Vary the ORDER as well as the wording -- givens-first and ask-first are both common in
        # real problems, and ordering moves 4-gram diversity more than the verb does.
        if rng.random() < 0.35:
            q = ask.rstrip(".?") + ("?" if ask.rstrip().endswith("?") else ".") + " " + \
                stem.strip().rstrip(",").rstrip(".") + "."
        else:
            q = stem + (ask[0].lower() + ask[1:] if stem.endswith(", ") else ask)
        umap = units_field(r)
        # ABSENCE MADE EXPLICIT. The negative existential -- "no value exists for this symbol" --
        # becomes a token lookup, the same move as fit for D2 and the tool call for arithmetic.
        # A19. COMPUTE `missing:` THE WAY THE DEVICE DOES -- src/store/assemble.c:82-89: the first
        # record variable, excluding the LHS and any supplied constant, that has no entered value.
        #
        # THE A11 FIX MOVED THE LEAK, IT DID NOT CLOSE IT. D2 hardcoded "none" while the record it
        # shows is a DIFFERENT record whose variables the question never binds, so `missing:none`
        # with unbound variables was a PERFECT classifier for D2: 166 of 166 against 0 of 2,424
        # answerable documents. The model could refuse every mismatch by checking whether the
        # record's variables appear in the question -- never reading what the record MEANS.
        #
        # Second time the same leak has been found one field over: A8 was the units field, A11 the
        # fit token, this is `missing:`. The lesson is not "fix this field" but that a refusal class
        # must be derived by the RUNTIME'S OWN RULE, so that any tell it leaves is one the device
        # leaves too. Computing it here means D1 and D2 both emit `missing:<var>` and differ only in
        # whether the record relates to the question -- which is the judgement.
        def _device_missing(rec, given_names):
            # FIRST-APPEARANCE ORDER, like units_field() and for the same reason: assemble.c walks
            # r->var[] in declaration order and returns the FIRST unbound one, so sorting picks a
            # different variable. `tau=R*C` gave missing:C where the device says missing:R.
            lhs_ = rec["f"].split("=", 1)[0].strip()
            _seen, _vs = set(), []
            for x in VAR.findall(rec["f"].split("=", 1)[1]):
                if x in ("pi", "e") or x in _seen: continue
                _seen.add(x); _vs.append(x)
            for v in _vs:
                if v == lhs_: continue
                if _const_for(rec, v) is not None: continue     # supplied constant, not asked for
                if v not in given_names: return v
            return "none"
        band = "high"          # A11: assemble.c:99 -- a real record is ALWAYS fit:high
        # A18. THE MISMATCH RECORD MUST ACTUALLY NOT FIT. `rng.choice(recs)` can draw a record
        # that computes the very quantity asked for -- 2 of 11,975 documents refused while showing
        # a record that answers the question, which is wrong supervision in the direction that
        # teaches over-refusal. Reject any candidate sharing the true record's LHS, and fall back
        # to the unfiltered draw only if the store somehow offers no alternative.
        rec_r = r
        if mismatch:
            _lhs = r["f"].split("=", 1)[0].strip()
            _alt = [x for x in recs if x["f"].split("=", 1)[0].strip() != _lhs]
            rec_r = rng.choice(_alt) if _alt else rng.choice(recs)
        _given = set(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)\s*=", g))
        # ONE PATH FOR EVERY CASE. D1 previously set this to `drop` directly; that happened to
        # agree, and "happens to agree" is how the other three fields drifted.
        miss = _device_missing(rec_r, _given)
        if mismatch:
            # A8. CALL units_field(), do not re-derive it. This branch carried its own copy that
            # read the RHS only, so the LHS symbol never got a unit -- and units_field()'s own
            # docstring records that exact defect being found and fixed. The fix reached one of the
            # two implementations. Third instance of the pattern, after the loss mask and genloop.
            #
            # The cost was not cosmetic. It made fit:low PERFECTLY PREDICTABLE from a formatting
            # bit: measured over 6,000 documents, the LHS unit was absent from 331/331 = 100.0% of
            # fit:low records and 42/5,669 = 0.7% of fit:high. A one-bit classifier separates the
            # refusal classes, so a model can learn "no LHS unit -> say it does not apply" without
            # ever weighing whether the record fits, and every D2 refusal metric would be measuring
            # the cue rather than the judgement. Same shape as the topic-scoping artefact: a number
            # that looks like a capability and is a property of the format.
            umap = units_field(rec_r)
        docs.append({"q": q, "withhold": drop if withhold else None, "nomatch": nomatch,
                     "mismatch": (rec_r.get("display") or rec_r.get("name","that quantity")) if mismatch else None,
                     # A9. THE CONDITION FIELD MUST MATCH THE SHIPPED ASSEMBLER, and it did not
                     # on 81 of 141 records (57.4%). src/store/assemble.c:94 is the authority:
                     #     r->req && r->req[0] ? r->req : "standard conditions"
                     # The generator instead used condition(name), a name-derived lookup, and only
                     # 2 of 141 store records carry `req` at all -- so the device says "standard
                     # conditions" where the model trained on "constant acceleration, no air
                     # resistance". A train/serve skew in the fourth field of every prompt.
                     #
                     # The name-derived conditions are arguably more informative, and that is not
                     # the deciding question: the model must be trained on what the device emits.
                     # Changing the device instead would be the other fix and it ships in C on
                     # hardware; if the richer conditions are wanted, they belong in the store's
                     # `req`, where BOTH producers already read them.
                     "rec": f"{rec_r['f']} | {umap} | missing:{miss} | "
                            f"{_condition_field(rec_r)} | fit:{band}", "lhs": lhs,
                     "name": r["name"], "head": r["f"], "unit": lhs_unit(r),
                     "close": rng.choice(CLOSE_ANY if rec_is_relation_named(r) else CLOSE_Q), "why": rng.choice(WHY).format(f=r["f"])})
        calls.append(f"<tool>eval<arg>{expr}</tool>")
    out = re.findall(r"<res>(.*?)</res>",
          subprocess.run(["tools/eval/evalcli","-"], input="\n".join(calls)+"\n",
                         capture_output=True, text=True).stdout, re.S)
    built, dropped = [], 0
    for d, c, res in zip(docs, calls, out):
        if d.get("nomatch"):
            # D3 -- THE SHAPE THE DEVICE EMITS WHEN THE PICKER FINDS NOTHING, and the corpus had
            # ZERO of them. Byte-identical to ns_assemble_none (src/store/assemble.c:109), which is
            # why the literal is written out here rather than composed from the five-field builder:
            # this prompt has no record, so it has no units, no condition and no formula, and
            # composing it would invite the fields back in.
            ans = "I cannot answer that — no record matches this question."
            built.append({"head": d["head"], "kind": "D3", "ans": ans,
                          "text": f"<q>{d['q']}</q><r>none | missing:none | "
                                  f"no matching relation | fit:low<a>{ans}<end>"})
            continue
        if d.get("mismatch"):
            ans=f"I cannot answer that — the record gives {d['mismatch'].lower()}, which does not apply."
            built.append({"head": d["head"], "kind": "D2",
                          "text": f"<q>{d['q']}</q><r>{d['rec']}<a>{ans}<end>", "ans": ans})
            continue
        if d.get("withhold"):
            # Same question, same vocabulary, same record -- one given absent and a refusal answer.
            ans = f"I cannot answer that — {d['withhold']} is not given."
            built.append({"head": d["head"], "kind": "D1",
                          "text": f"<q>{d['q']}</q><r>{d['rec']}<a>{ans}<end>", "ans": ans})
            continue
        if res.startswith("!"): dropped += 1; continue        # TOOL_SPEC 8.1: drop, never guess
        _rec = next((x for x in recs if x["f"] == d["head"]), None)
        if _rec is not None:
            try:    _why = implausible(_rec, float(str(res).split()[0]))
            except (ValueError, IndexError): _why = None
            if _why:
                dropped += 1; continue                # A17: drop, do not teach an impossible value
        # A20. `<res>` IS THE RUNTIME'S STRING, VERBATIM. The 4-significant-figure sign-off was
        # about the ANSWER's precision and this line applied it to the INJECTED RESULT as well,
        # rebinding `res` before it reached the <res> span four lines below.
        #
        # src/store/toolrun.c:34 writes `<res>%s</res>` straight from the evaluator, and
        # tools/eval/fmt.c:31 is SIG_DIGITS 10. So 52.88% of answerable documents carried a result
        # string the shipped runtime does not produce -- `9.541e-32` where the device emits
        # `9.54144e-32`. The A9 pattern exactly: two producers of one field, one changed, the other
        # not, and this one is the ARCHITECTURE'S CENTRAL MECHANISM.
        #
        # And the answer restated <res> byte-for-byte in 100% of documents, so the corpus never
        # demonstrated a rounding step. At serve time the model is handed a 10-digit result in a
        # slot where it has only ever seen 4 -- having learned that the answer is the res span
        # copied. Rounding in the ANSWER is what the sign-off asked for, and it is now the only
        # place it happens, which also gives the model the rounding step to learn.
        a_val = res
        try:   a_val = f"{float(res):.4g}"          # signed off: 4 significant figures, ANSWER only
        except ValueError: pass
        a_txt = f"{a_val} {d['unit']}".strip() if d.get("unit") else a_val
        ans = d["close"].format(v=d["lhs"], a=a_txt, q=d["name"].lower(), why=d["why"])
        built.append({"head": d["head"],
                      "text": f"<q>{d['q']}</q><r>{d['rec']}{c}<res>{res}</res><a>{ans}<end>",
                      "ans": ans})
    return built, dropped

# ---- diversity metrics -------------------------------------------------------
def ngrams(s, n=4):
    w = s.split()
    return [tuple(w[i:i+n]) for i in range(max(0, len(w)-n+1))]

def diversity_by_span(docs):
    """Per-span diversity. The composite ratio averages over spans with DIFFERENT ROLES and is
    therefore the wrong instrument: the record span is a canonical retrieval record and is SUPPOSED
    to repeat -- it carries 25% of the corpus's 4-grams at a ratio of 0.0002, dragging the composite
    down for a reason that is not a defect. Question and answer diversity are what the model must
    learn from; record diversity is bounded by head count and nothing else."""
    def ng(s, n=4):
        w = s.split(); return [tuple(w[i:i+n]) for i in range(max(0, len(w)-n+1))]
    out = {}
    for lab, get in (("question", lambda d: d["text"].split("<q>")[1].split("</q>")[0]),
                     ("record",   lambda d: d["text"].split("<r>")[1].split("<tool>")[0]),
                     ("answer",   lambda d: d["ans"])):
        gs = [g for d in docs for g in ng(get(d))]
        out[lab] = len(set(gs)) / max(1, len(gs))
    return out

def diversity(docs):
    allg = [g for d in docs for g in ngrams(d["text"])]
    heads = collections.Counter(d["head"] for d in docs)
    # phrasing entropy per head: how many distinct answer shapes each head appears in
    per = collections.defaultdict(set)
    for d in docs: per[d["head"]].add(re.sub(r"[-\d.]+", "#", d["ans"]))
    ent = []
    for h, shapes in per.items():
        c = collections.Counter()
        for d in docs:
            if d["head"] == h: c[re.sub(r"[-\d.]+", "#", d["ans"])] += 1
        tot = sum(c.values())
        ent.append(-sum((v/tot)*math.log2(v/tot) for v in c.values()) if tot > 1 else 0.0)
    return {"distinct_4gram_ratio": len(set(allg))/max(1, len(allg)),
            "head_coverage": len(heads)/len(recs),
            "heads_used": len(heads),
            "mean_phrasing_entropy_bits": sum(ent)/max(1, len(ent))}

if __name__ == "__main__":
    N = int(sys.argv[1]) if len(sys.argv) > 1 else 10000
    t0 = time.time(); docs, dropped = gen(N, seed=20260820); el = time.time()-t0
    chars = sum(len(d["text"]) for d in docs)
    D = diversity(docs)
    print(f"\ngenerated {len(docs):,} documents ({dropped} dropped on evaluator error) in {el:.1f}s")
    print(f"  THROUGHPUT      {len(docs)/el:,.0f} docs/s   ->  185M tokens in "
          f"{185e6*4.15/(chars/len(docs))/(len(docs)/el)/3600:.2f} h")
    print(f"  chars/doc       {chars/len(docs):.0f}")
    print(f"  tokens @4.15    {chars/4.15/1e6:.2f}M from this run")
    print(f"\n  DIVERSITY  (never report the token count without these)")
    print(f"    composite (RETIRED)          {D['distinct_4gram_ratio']:.4f} @{len(docs):,} docs")
    print(f"    head coverage                {D['head_coverage']*100:.1f}%  ({D['heads_used']} heads)")
    print(f"    mean phrasing entropy        {D['mean_phrasing_entropy_bits']:.2f} bits/head")
    SP = diversity_by_span(docs)
    # Corpus size is stamped on every diversity figure: these ratios fall with document count, so
    # a figure quoted without its N is not comparable to any other. I once compared a 200k run
    # against 100k arm figures and read a real gain as a loss.
    print(f"    per-span 4-gram @{len(docs):,} docs  question {SP['question']:.4f}   "
          f"record {SP['record']:.4f}   answer {SP['answer']:.4f}")
    print(f"      (the composite above is dragged down by the record span, which is a canonical")
    print(f"       retrieval record and is SUPPOSED to repeat -- bounded by head count alone)")
    D.update({"span_"+k: v for k, v in SP.items()})
    _wt("corpus/synth_sample.jsonl", 
        "\n".join(json.dumps(d) for d in docs))
    _wj("corpus/diversity.json", D, indent=1)
    print("\n  sample:"); [print("   ", d["text"][:150]) for d in docs[:3]]
