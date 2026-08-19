/* numeric.c -- numeric evaluation of an AST, with dimensional analysis.
 *
 * Units flow through the arithmetic automatically: every value is a quantity (SI magnitude +
 * dimension vector), so "9.8 m/s^2 * 3 s" yields 29.4 with dimension m/s without any special case.
 * Adding incompatible dimensions is E_UNITS, which is the whole point of carrying them.
 */
#include "eval.h"
#include <math.h>
#include <string.h>

static const dim_t DIM_NONE = {{0,0,0,0,0,0,0}};

static err_t bound(const binds_t *b, const char *name, double *v) {
    if (!b) return E_EXPR;
    for (int i = 0; i < b->n; i++)
        if (strcmp(b->name[i], name) == 0) { *v = b->val[i]; return E_NONE; }
    return E_EXPR;
}

/* Powers must have a scalar exponent. A non-integer exponent on a dimensioned base is meaningless
 * (what is the dimension of m^0.5 in a physics answer?) so we refuse rather than guess. */
static err_t do_pow(quant_t base, quant_t ex, quant_t *out) {
    if (!dim_is_scalar(ex.d)) return E_UNITS;
    if (dim_is_scalar(base.d)) {
        if (base.v < 0 && ex.v != floor(ex.v)) return E_DOMAIN;
        if (base.v == 0 && ex.v < 0) return E_DOMAIN;
        out->v = pow(base.v, ex.v);
        out->d = DIM_NONE;
    } else {
        if (ex.v != floor(ex.v)) return E_UNITS;
        int k = (int)ex.v;
        if (k < -32 || k > 32) return E_RANGE;
        out->v = pow(base.v, ex.v);
        out->d = dim_scale(base.d, k);
    }
    if (isnan(out->v)) return E_DOMAIN;
    if (isinf(out->v)) return E_RANGE;
    return E_NONE;
}

static err_t call_fn(const char *name, quant_t *a, int n, quant_t *out) {
    /* Everything here is dimensionless in and out. A transcendental of a dimensioned quantity is a
     * category error, so it is E_UNITS rather than a silent strip. */
    if (strcmp(name, "min") == 0 || strcmp(name, "max") == 0) {
        if (n != 2) return E_ARITY;
        if (!dim_eq(a[0].d, a[1].d)) return E_UNITS;
        out->d = a[0].d;
        out->v = (strcmp(name, "min") == 0) ? (a[0].v < a[1].v ? a[0].v : a[1].v)
                                            : (a[0].v > a[1].v ? a[0].v : a[1].v);
        return E_NONE;
    }
    if (n != 1) return E_ARITY;

    if (strcmp(name, "abs") == 0)   { out->v = fabs(a[0].v);  out->d = a[0].d; return E_NONE; }
    if (strcmp(name, "floor") == 0) { out->v = floor(a[0].v); out->d = a[0].d; return E_NONE; }
    if (strcmp(name, "ceil") == 0)  { out->v = ceil(a[0].v);  out->d = a[0].d; return E_NONE; }
    if (strcmp(name, "round") == 0) { out->v = floor(a[0].v + 0.5); out->d = a[0].d; return E_NONE; }
    if (strcmp(name, "sqrt") == 0) {
        if (a[0].v < 0) return E_DOMAIN;
        for (int i = 0; i < DIM_COUNT; i++) if (a[0].d.e[i] % 2) return E_UNITS;
        out->v = sqrt(a[0].v);
        out->d = a[0].d;
        for (int i = 0; i < DIM_COUNT; i++) out->d.e[i] = (signed char)(out->d.e[i] / 2);
        return E_NONE;
    }
    if (strcmp(name, "fact") == 0) {
        if (!dim_is_scalar(a[0].d)) return E_UNITS;
        if (a[0].v < 0 || a[0].v != floor(a[0].v) || a[0].v > 170) return E_DOMAIN;
        double r = 1;
        for (int i = 2; i <= (int)a[0].v; i++) r *= i;
        out->v = r; out->d = DIM_NONE; return E_NONE;
    }

    if (!dim_is_scalar(a[0].d)) return E_UNITS;
    double x = a[0].v, r;
    if      (strcmp(name, "sin") == 0)  r = sin(x);
    else if (strcmp(name, "cos") == 0)  r = cos(x);
    else if (strcmp(name, "tan") == 0)  r = tan(x);
    else if (strcmp(name, "sinh") == 0) r = sinh(x);
    else if (strcmp(name, "cosh") == 0) r = cosh(x);
    else if (strcmp(name, "tanh") == 0) r = tanh(x);
    else if (strcmp(name, "exp") == 0)  r = exp(x);
    else if (strcmp(name, "asin") == 0) { if (x < -1 || x > 1) return E_DOMAIN; r = asin(x); }
    else if (strcmp(name, "acos") == 0) { if (x < -1 || x > 1) return E_DOMAIN; r = acos(x); }
    else if (strcmp(name, "atan") == 0) r = atan(x);
    else if (strcmp(name, "ln") == 0)   { if (x <= 0) return E_DOMAIN; r = log(x); }
    else if (strcmp(name, "log") == 0 || strcmp(name, "log10") == 0)
                                        { if (x <= 0) return E_DOMAIN; r = log10(x); }
    else return E_NAME;

    if (isnan(r)) return E_DOMAIN;
    if (isinf(r)) return E_RANGE;
    out->v = r; out->d = DIM_NONE;
    return E_NONE;
}

err_t num_eval(const node_t *n, const binds_t *b, int use_units, quant_t *out) {
    if (!n) return E_EXPR;
    quant_t l, r;
    err_t e;

    switch (n->t) {
    case N_NUM:
        out->v = n->num; out->d = DIM_NONE; return E_NONE;

    case N_SYM: {
        double v;
        if (bound(b, n->name, &v) == E_NONE) { out->v = v; out->d = DIM_NONE; return E_NONE; }
        if (strcmp(n->name, "pi") == 0) { out->v = 3.14159265358979323846; out->d = DIM_NONE; return E_NONE; }
        if (strcmp(n->name, "e")  == 0) { out->v = 2.71828182845904523536; out->d = DIM_NONE; return E_NONE; }
        if (use_units) {
            quant_t q; double off;
            if (units_lookup(n->name, &q, &off)) {
                if (off != 0.0) return E_UNITS;   /* affine units are conv-only, see units.c */
                *out = q; return E_NONE;
            }
        }
        return E_EXPR;                            /* unbound symbol */
    }

    case N_NEG:
        e = num_eval(n->kid[0], b, use_units, &l); if (e) return e;
        out->v = -l.v; out->d = l.d; return E_NONE;

    case N_ADD: case N_SUB:
        e = num_eval(n->kid[0], b, use_units, &l); if (e) return e;
        e = num_eval(n->kid[1], b, use_units, &r); if (e) return e;
        if (!dim_eq(l.d, r.d)) return E_UNITS;
        out->v = (n->t == N_ADD) ? l.v + r.v : l.v - r.v;
        out->d = l.d;
        break;

    case N_MUL:
        e = num_eval(n->kid[0], b, use_units, &l); if (e) return e;
        e = num_eval(n->kid[1], b, use_units, &r); if (e) return e;
        out->v = l.v * r.v; out->d = dim_add(l.d, r.d);
        break;

    case N_DIV:
        e = num_eval(n->kid[0], b, use_units, &l); if (e) return e;
        e = num_eval(n->kid[1], b, use_units, &r); if (e) return e;
        if (r.v == 0.0) return E_DOMAIN;
        out->v = l.v / r.v; out->d = dim_sub(l.d, r.d);
        break;

    case N_POW:
        e = num_eval(n->kid[0], b, use_units, &l); if (e) return e;
        e = num_eval(n->kid[1], b, use_units, &r); if (e) return e;
        return do_pow(l, r, out);

    case N_CALL: {
        quant_t a[MAX_ARGS];
        for (int i = 0; i < n->nkid; i++) {
            e = num_eval(n->kid[i], b, use_units, &a[i]);
            if (e) return e;
        }
        return call_fn(n->name, a, n->nkid, out);
    }

    case N_EQ:
        return E_EXPR;                            /* an equation is not a value */
    }

    if (isnan(out->v)) return E_DOMAIN;
    if (isinf(out->v)) return E_RANGE;
    return E_NONE;
}
