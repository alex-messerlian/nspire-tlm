/* units.c -- unit table and dimensional arithmetic.
 *
 * Units are just identifiers that resolve to a quantity, so "9.8 m/s^2" falls out of the ordinary
 * expression grammar via implicit multiplication -- no special-case parsing needed.
 *
 * Affine units (degC, degF) carry an offset and are legal ONLY as a whole conv argument. Allowing
 * them inside compound expressions is the classic units-library bug: "20 degC * 2" has no meaning,
 * and silently treating it as 20 K * 2 is worse than refusing.
 */
#include "eval.h"
#include <string.h>
#include <stdio.h>

int dim_eq(dim_t a, dim_t b) {
    for (int i = 0; i < DIM_COUNT; i++) if (a.e[i] != b.e[i]) return 0;
    return 1;
}
int dim_is_scalar(dim_t d) {
    for (int i = 0; i < DIM_COUNT; i++) if (d.e[i]) return 0;
    return 1;
}
dim_t dim_add(dim_t a, dim_t b) {
    for (int i = 0; i < DIM_COUNT; i++) a.e[i] = (signed char)(a.e[i] + b.e[i]);
    return a;
}
dim_t dim_sub(dim_t a, dim_t b) {
    for (int i = 0; i < DIM_COUNT; i++) a.e[i] = (signed char)(a.e[i] - b.e[i]);
    return a;
}
dim_t dim_scale(dim_t a, int k) {
    for (int i = 0; i < DIM_COUNT; i++) a.e[i] = (signed char)(a.e[i] * k);
    return a;
}

/* {m, kg, s, A, K, mol, cd} */
#define D0        {{ 0, 0, 0, 0, 0, 0, 0}}
#define D_M       {{ 1, 0, 0, 0, 0, 0, 0}}
#define D_KG      {{ 0, 1, 0, 0, 0, 0, 0}}
#define D_S       {{ 0, 0, 1, 0, 0, 0, 0}}
#define D_A       {{ 0, 0, 0, 1, 0, 0, 0}}
#define D_K       {{ 0, 0, 0, 0, 1, 0, 0}}
#define D_MOL     {{ 0, 0, 0, 0, 0, 1, 0}}
#define D_CD      {{ 0, 0, 0, 0, 0, 0, 1}}
#define D_N       {{ 1, 1,-2, 0, 0, 0, 0}}
#define D_J       {{ 2, 1,-2, 0, 0, 0, 0}}
#define D_W       {{ 2, 1,-3, 0, 0, 0, 0}}
#define D_PA      {{-1, 1,-2, 0, 0, 0, 0}}
#define D_HZ      {{ 0, 0,-1, 0, 0, 0, 0}}
#define D_C       {{ 0, 0, 1, 1, 0, 0, 0}}
#define D_V       {{ 2, 1,-3,-1, 0, 0, 0}}
#define D_OHM     {{ 2, 1,-3,-2, 0, 0, 0}}
#define D_F       {{-2,-1, 4, 2, 0, 0, 0}}
#define D_T       {{ 0, 1,-2,-1, 0, 0, 0}}
#define D_M2      {{ 2, 0, 0, 0, 0, 0, 0}}
#define D_M3      {{ 3, 0, 0, 0, 0, 0, 0}}
#define D_MS      {{ 1, 0,-1, 0, 0, 0, 0}}
#define D_MS2     {{ 1, 0,-2, 0, 0, 0, 0}}

typedef struct { const char *name; double scale; dim_t d; double offset; } unit_t;

static const unit_t UNITS[] = {
    /* SI base */
    {"m",1,D_M,0},{"kg",1,D_KG,0},{"s",1,D_S,0},{"A",1,D_A,0},{"K",1,D_K,0},
    {"mol",1,D_MOL,0},{"cd",1,D_CD,0},
    /* length */
    {"km",1e3,D_M,0},{"cm",1e-2,D_M,0},{"mm",1e-3,D_M,0},{"um",1e-6,D_M,0},{"nm",1e-9,D_M,0},
    {"mi",1609.344,D_M,0},{"ft",0.3048,D_M,0},{"in",0.0254,D_M,0},{"yd",0.9144,D_M,0},
    /* mass */
    {"g",1e-3,D_KG,0},{"mg",1e-6,D_KG,0},{"t",1e3,D_KG,0},
    {"lb",0.45359237,D_KG,0},{"oz",0.028349523125,D_KG,0},
    /* time */
    {"ms",1e-3,D_S,0},{"us",1e-6,D_S,0},{"ns",1e-9,D_S,0},
    {"min",60,D_S,0},{"h",3600,D_S,0},{"day",86400,D_S,0},{"yr",31557600,D_S,0},
    /* derived */
    {"N",1,D_N,0},{"kN",1e3,D_N,0},
    {"J",1,D_J,0},{"kJ",1e3,D_J,0},{"MJ",1e6,D_J,0},
    {"W",1,D_W,0},{"kW",1e3,D_W,0},{"MW",1e6,D_W,0},
    {"Pa",1,D_PA,0},{"kPa",1e3,D_PA,0},{"MPa",1e6,D_PA,0},{"GPa",1e9,D_PA,0},
    {"Hz",1,D_HZ,0},{"kHz",1e3,D_HZ,0},{"MHz",1e6,D_HZ,0},{"GHz",1e9,D_HZ,0},
    {"C",1,D_C,0},{"V",1,D_V,0},{"mV",1e-3,D_V,0},{"kV",1e3,D_V,0},
    {"ohm",1,D_OHM,0},{"kohm",1e3,D_OHM,0},{"Mohm",1e6,D_OHM,0},
    {"mA",1e-3,D_A,0},{"uA",1e-6,D_A,0},
    {"F",1,D_F,0},{"uF",1e-6,D_F,0},{"nF",1e-9,D_F,0},{"pF",1e-12,D_F,0},
    {"T",1,D_T,0},{"mT",1e-3,D_T,0},
    /* area / volume */
    {"L",1e-3,D_M3,0},{"mL",1e-6,D_M3,0},{"gal",0.003785411784,D_M3,0},
    /* energy, pressure, misc */
    {"eV",1.602176634e-19,D_J,0},{"cal",4.184,D_J,0},{"kcal",4184,D_J,0},
    {"kWh",3.6e6,D_J,0},{"atm",101325,D_PA,0},{"bar",1e5,D_PA,0},
    /* angle: dimensionless, radians are the base */
    {"rad",1,D0,0},{"deg",0.017453292519943295,D0,0},{"rev",6.283185307179586,D0,0},
    /* affine -- conv only */
    {"degC",1,D_K,273.15},{"degF",0.5555555555555556,D_K,255.3722222222222},
};
#define NUNITS ((int)(sizeof UNITS / sizeof UNITS[0]))

int units_lookup(const char *name, quant_t *q, double *offset) {
    for (int i = 0; i < NUNITS; i++) {
        if (strcmp(UNITS[i].name, name) == 0) {
            q->v = UNITS[i].scale;
            q->d = UNITS[i].d;
            if (offset) *offset = UNITS[i].offset;
            return 1;
        }
    }
    return 0;
}

/* Canonical rendering. Positive exponents first in fixed base order, then '/' and the negatives.
 * Fixed order is what makes this deterministic. */
static void emit_dim(char *p, size_t *k, size_t sz, const char *sym, int exp) {
    char tmp[16];
    int len;
    if (exp == 1) len = snprintf(tmp, sizeof tmp, "%s", sym);
    else          len = snprintf(tmp, sizeof tmp, "%s^%d", sym, exp);
    if (len < 0) return;
    if (*k + (size_t)len + 2 >= sz) return;
    if (*k > 0 && p[*k - 1] != '/') { p[(*k)++] = '*'; }
    memcpy(p + *k, tmp, (size_t)len);
    *k += (size_t)len;
    p[*k] = 0;
}

/* Coherent SI derived units, checked before falling back to base units. Rendering kinetic energy
 * as "16000 J" rather than "16000 m^2*kg/s^2" matters: the physics corpus is generated from these
 * strings, and the base-unit form is not what a textbook answer looks like.
 *
 * Fixed table, fixed order, so this stays deterministic.
 *
 * BLOCKLIST (TOOL_SPEC.md section 5.4, v1.1). Two dimension vectors are genuinely ambiguous and are
 * deliberately ABSENT from this table, so they fall through to base units:
 *
 *   m^2*kg/s^2  energy (J) vs torque (N*m)
 *   1/s         frequency (Hz) vs angular velocity (rad/s) vs decay constant
 *
 * A dimension vector cannot distinguish these, and guessing compiles a physics error straight into
 * the weights -- "torque is measured in joules" is the first thing a physics judge catches. The data
 * generator must name the target unit explicitly with conv (TOOL_SPEC.md section 8.5).
 *
 * Deliberately NOT blocked: Pa is shared by pressure, stress and Young's modulus, but all three are
 * correctly written Pa, so the collapse is right in every case. */
static const struct { dim_t d; const char *sym; } DERIVED[] = {
    {D_N,"N"},{D_W,"W"},{D_PA,"Pa"},{D_V,"V"},{D_OHM,"ohm"},
    {D_F,"F"},{D_T,"T"},{D_C,"C"},
};
#define NDERIVED ((int)(sizeof DERIVED / sizeof DERIVED[0]))

void units_render(dim_t d, char *out, size_t sz) {
    static const char *SYM[DIM_COUNT] = {"m","kg","s","A","K","mol","cd"};
    for (int i = 0; i < NDERIVED; i++) {
        if (dim_eq(d, DERIVED[i].d)) {
            size_t L = strlen(DERIVED[i].sym);
            if (L + 1 <= sz) { memcpy(out, DERIVED[i].sym, L + 1); return; }
        }
    }
    size_t k = 0;
    out[0] = 0;
    for (int i = 0; i < DIM_COUNT; i++) if (d.e[i] > 0) emit_dim(out, &k, sz, SYM[i], d.e[i]);
    int anyneg = 0;
    for (int i = 0; i < DIM_COUNT; i++) if (d.e[i] < 0) anyneg = 1;
    if (!anyneg) return;
    if (k == 0) { if (sz > 1) { out[k++] = '1'; out[k] = 0; } }
    if (k + 1 < sz) { out[k++] = '/'; out[k] = 0; }
    for (int i = 0; i < DIM_COUNT; i++) if (d.e[i] < 0) emit_dim(out, &k, sz, SYM[i], -d.e[i]);
}
