/* solve.c -- linear and quadratic equation solving via polynomial coefficient extraction.
 *
 * Strategy: rewrite lhs=rhs as lhs-rhs=0, then walk the tree accumulating coefficients of powers of
 * the target variable. Anything that is not a polynomial in that variable -- sin(x), 1/x, x^y --
 * makes extraction fail and we return !nosol. Refusing cleanly is a feature: the model can learn to
 * route around !nosol, but it cannot learn to distrust a confidently wrong root.
 */
#include "eval.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define MAXDEG 2
typedef struct { double c[MAXDEG + 1]; int ok; } poly_t;

static poly_t p_const(double v)  { poly_t p = {{0,0,0},1}; p.c[0] = v; return p; }
static poly_t p_fail(void)       { poly_t p = {{0,0,0},0}; return p; }

static poly_t p_add(poly_t a, poly_t b, int sign) {
    if (!a.ok || !b.ok) return p_fail();
    for (int i = 0; i <= MAXDEG; i++) a.c[i] += sign * b.c[i];
    return a;
}

static poly_t p_mul(poly_t a, poly_t b) {
    if (!a.ok || !b.ok) return p_fail();
    poly_t r = {{0,0,0},1};
    for (int i = 0; i <= MAXDEG; i++)
        for (int j = 0; j <= MAXDEG; j++) {
            if (a.c[i] == 0 || b.c[j] == 0) continue;
            if (i + j > MAXDEG) return p_fail();     /* degree > 2 -- outside our scope */
            r.c[i + j] += a.c[i] * b.c[j];
        }
    return r;
}

static poly_t extract(const node_t *n, const char *var, int depth) {
    if (!n || depth > MAX_DEPTH) return p_fail();
    switch (n->t) {
    case N_NUM: return p_const(n->num);
    case N_SYM:
        if (strcmp(n->name, var) == 0) { poly_t p = {{0,1,0},1}; return p; }
        if (strcmp(n->name, "pi") == 0) return p_const(3.14159265358979323846);
        if (strcmp(n->name, "e")  == 0) return p_const(2.71828182845904523536);
        return p_fail();                              /* another free symbol: underdetermined */
    case N_NEG: {
        poly_t a = extract(n->kid[0], var, depth + 1);
        if (!a.ok) return p_fail();
        for (int i = 0; i <= MAXDEG; i++) a.c[i] = -a.c[i];
        return a;
    }
    case N_ADD: return p_add(extract(n->kid[0], var, depth+1), extract(n->kid[1], var, depth+1),  1);
    case N_SUB: return p_add(extract(n->kid[0], var, depth+1), extract(n->kid[1], var, depth+1), -1);
    case N_MUL: return p_mul(extract(n->kid[0], var, depth+1), extract(n->kid[1], var, depth+1));
    case N_DIV: {
        poly_t a = extract(n->kid[0], var, depth + 1);
        poly_t b = extract(n->kid[1], var, depth + 1);
        if (!a.ok || !b.ok) return p_fail();
        if (b.c[1] != 0 || b.c[2] != 0) return p_fail();   /* var in a denominator */
        if (b.c[0] == 0) return p_fail();
        for (int i = 0; i <= MAXDEG; i++) a.c[i] /= b.c[0];
        return a;
    }
    case N_POW: {
        if (n->kid[1]->t != N_NUM) return p_fail();
        double k = n->kid[1]->num;
        if (k != floor(k) || k < 0 || k > MAXDEG) return p_fail();
        poly_t base = extract(n->kid[0], var, depth + 1);
        if (!base.ok) return p_fail();
        poly_t r = p_const(1);
        for (int i = 0; i < (int)k; i++) { r = p_mul(r, base); if (!r.ok) return p_fail(); }
        return r;
    }
    case N_CALL: {
        /* A function of a constant is a constant; a function of the variable is not polynomial. */
        quant_t q;
        binds_t nb = {{0},{0},0};
        if (num_eval(n, &nb, 0, &q) == E_NONE && dim_is_scalar(q.d)) return p_const(q.v);
        return p_fail();
    }
    default: return p_fail();
    }
}

/* Roots are emitted in ascending order because unordered output is nondeterministic output
 * (TOOL_SPEC.md section 5.2). */
static err_t emit_roots(const char *var, double *r, int n, char *out, size_t sz) {
    if (n == 2 && r[0] > r[1]) { double t = r[0]; r[0] = r[1]; r[1] = t; }
    size_t k = 0;
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        char num[64];
        err_t e = fmt_number(r[i], num, sizeof num);
        if (e) return e;
        int need = (int)strlen(var) + 1 + (int)strlen(num) + (i ? 2 : 0);
        if (k + (size_t)need + 1 > sz) return E_RANGE;
        if (i) { out[k++] = ','; out[k++] = ' '; }
        k += (size_t)snprintf(out + k, sz - k, "%s=%s", var, num);
    }
    return E_NONE;
}

err_t solve_eq(arena_t *a, const node_t *eq, const char *var, char *out, size_t sz) {
    (void)a;
    const node_t *lhs, *rhs_is_zero = NULL;
    poly_t p;

    if (eq->t == N_EQ) {
        poly_t l = extract(eq->kid[0], var, 0);
        poly_t r = extract(eq->kid[1], var, 0);
        p = p_add(l, r, -1);
    } else {
        lhs = eq; (void)rhs_is_zero;
        p = extract(lhs, var, 0);                    /* bare expression: solve expr = 0 */
    }
    if (!p.ok) return E_NOSOL;

    double c = p.c[0], b = p.c[1], A = p.c[2];

    if (A == 0.0 && b == 0.0) {
        if (c == 0.0) { if (sz < 4) return E_RANGE; strcpy(out, "all");  return E_NONE; }
        else          { if (sz < 5) return E_RANGE; strcpy(out, "none"); return E_NONE; }
    }
    if (A == 0.0) {
        double root = -c / b;
        return emit_roots(var, &root, 1, out, sz);
    }

    double disc = b * b - 4 * A * c;
    if (disc > 0) {
        /* Numerically stable form: computing both roots as (-b±sqrt)/2a loses precision in the
         * root where the subtraction cancels. */
        double sq = sqrt(disc);
        double q = -0.5 * (b + (b >= 0 ? sq : -sq));
        double r[2] = { q / A, c / q };
        return emit_roots(var, r, 2, out, sz);
    }
    if (disc == 0) {
        double root = -b / (2 * A);
        return emit_roots(var, &root, 1, out, sz);
    }
    /* Complex conjugate pair, real part first (TOOL_SPEC.md section 5.2). */
    {
        double re = -b / (2 * A), im = sqrt(-disc) / (2 * fabs(A));
        char rs[64], is[64];
        err_t e;
        if ((e = fmt_number(re, rs, sizeof rs))) return e;
        if ((e = fmt_number(im, is, sizeof is))) return e;
        if (strlen(rs) + strlen(is) + 2 * strlen(var) + 12 > sz) return E_RANGE;
        snprintf(out, sz, "%s=%s+%si, %s=%s-%si", var, rs, is, var, rs, is);
        return E_NONE;
    }
}
