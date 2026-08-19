/* literal.c -- E1/E2/E3r: solving equations whose coefficients are SYMBOLIC.
 *
 * solve.c extracts polynomial coefficients as doubles, which handles 2x+6=0 and fails on P=2l+2w.
 * This file does the same extraction with AST-NODE coefficients, so the target variable can be
 * isolated even when everything else is a free symbol.
 *
 * Measured motivation (docs/P1_PROBE_PRELIM.md): 0 of 72 intro-physics formula rearrangements
 * solved before this. E1 alone covers 53, +E2 covers 12 more, +E3r covers 6 more -> 99%.
 *
 *   E1  linear literal isolation   A*t + B = 0            -> t = -B/A
 *   E2  denominator clearing       v = d/t                -> multiply through, then E1
 *   E3r root extraction            A*t^2 + C = 0, B == 0  -> t = +-sqrt(-C/A)
 *
 * NOT implemented, by decision: full quadratic literal solve (target at degree 1 AND 2 with symbolic
 * coefficients). One item in the probe set, needs a symbolic discriminant and a root-sign
 * convention, and returns !nosol here.
 */
#include "eval.h"
#include <string.h>
#include <stdio.h>

#define LDEG 2

/* Symbolic polynomial in the target variable: c[k] is the AST for the coefficient of var^k.
 * NULL coefficient means zero -- distinct from a node holding literal 0, so callers must use
 * the helpers rather than testing pointers ad hoc. */
typedef struct { node_t *c[LDEG + 1]; int ok; } lpoly_t;

static lpoly_t lp_fail(void) { lpoly_t p; memset(&p, 0, sizeof p); p.ok = 0; return p; }

static lpoly_t lp_const(node_t *n) {
    lpoly_t p; memset(&p, 0, sizeof p);
    p.ok = 1; p.c[0] = n;
    return p;
}

/* Does this subtree mention the target variable? Used to decide whether a node can serve as a
 * coefficient (it cannot, if it contains the variable we are solving for). */
static int mentions(const node_t *n, const char *var, int depth) {
    if (!n || depth > MAX_DEPTH) return 0;
    if (n->t == N_SYM) return strcmp(n->name, var) == 0;
    for (int i = 0; i < n->nkid; i++)
        if (mentions(n->kid[i], var, depth + 1)) return 1;
    return 0;
}

static node_t *sym_add(arena_t *a, node_t *x, node_t *y) {
    if (!x) return y;
    if (!y) return x;
    return ar_bin(a, N_ADD, x, y);
}
static node_t *sym_neg(arena_t *a, node_t *x) {
    if (!x) return NULL;
    node_t *n = ar_new(a, N_NEG);
    if (!n) return NULL;
    n->kid[0] = x; n->nkid = 1;
    return n;
}
static node_t *sym_mul(arena_t *a, node_t *x, node_t *y) {
    if (!x || !y) return NULL;                 /* zero times anything is zero */
    return ar_bin(a, N_MUL, x, y);
}

static lpoly_t lp_add(arena_t *a, lpoly_t x, lpoly_t y, int sign) {
    if (!x.ok || !y.ok) return lp_fail();
    lpoly_t r; memset(&r, 0, sizeof r); r.ok = 1;
    for (int i = 0; i <= LDEG; i++) {
        node_t *yi = y.c[i];
        if (yi && sign < 0) yi = sym_neg(a, yi);
        r.c[i] = sym_add(a, x.c[i], yi);
    }
    return r;
}

static lpoly_t lp_mul(arena_t *a, lpoly_t x, lpoly_t y) {
    if (!x.ok || !y.ok) return lp_fail();
    lpoly_t r; memset(&r, 0, sizeof r); r.ok = 1;
    for (int i = 0; i <= LDEG; i++)
        for (int j = 0; j <= LDEG; j++) {
            if (!x.c[i] || !y.c[j]) continue;
            if (i + j > LDEG) return lp_fail();      /* degree > 2: out of scope */
            r.c[i + j] = sym_add(a, r.c[i + j], sym_mul(a, x.c[i], y.c[j]));
        }
    return r;
}

/* Extract a symbolic polynomial in `var`. Subtrees free of `var` become coefficients verbatim. */
static lpoly_t lextract(arena_t *a, const node_t *n, const char *var, int depth) {
    if (!n || depth > MAX_DEPTH) return lp_fail();

    if (!mentions(n, var, 0)) {                       /* whole subtree is a coefficient */
        node_t *c = ar_clone(a, n);
        return c ? lp_const(c) : lp_fail();
    }

    switch (n->t) {
    case N_SYM: {                                     /* must be the target itself */
        lpoly_t p; memset(&p, 0, sizeof p);
        p.ok = 1;
        p.c[1] = ar_num(a, 1);
        return p;
    }
    case N_NEG: {
        lpoly_t x = lextract(a, n->kid[0], var, depth + 1);
        if (!x.ok) return lp_fail();
        for (int i = 0; i <= LDEG; i++) if (x.c[i]) x.c[i] = sym_neg(a, x.c[i]);
        return x;
    }
    case N_ADD: return lp_add(a, lextract(a, n->kid[0], var, depth+1),
                                 lextract(a, n->kid[1], var, depth+1),  1);
    case N_SUB: return lp_add(a, lextract(a, n->kid[0], var, depth+1),
                                 lextract(a, n->kid[1], var, depth+1), -1);
    case N_MUL: return lp_mul(a, lextract(a, n->kid[0], var, depth+1),
                                 lextract(a, n->kid[1], var, depth+1));
    case N_DIV: {
        /* var in the denominator is E2's job, handled before we get here. If it still appears,
         * this expression is not polynomial in var. */
        if (mentions(n->kid[1], var, 0)) return lp_fail();
        lpoly_t x = lextract(a, n->kid[0], var, depth + 1);
        if (!x.ok) return lp_fail();
        node_t *den = ar_clone(a, n->kid[1]);
        if (!den) return lp_fail();
        for (int i = 0; i <= LDEG; i++)
            if (x.c[i]) x.c[i] = ar_bin(a, N_DIV, x.c[i], ar_clone(a, den));
        return x;
    }
    case N_POW: {
        if (n->kid[1]->t != N_NUM) return lp_fail();
        double k = n->kid[1]->num;
        if (k != (double)(int)k || k < 0 || k > LDEG) return lp_fail();
        lpoly_t base = lextract(a, n->kid[0], var, depth + 1);
        if (!base.ok) return lp_fail();
        lpoly_t r = lp_const(ar_num(a, 1));
        for (int i = 0; i < (int)k; i++) { r = lp_mul(a, r, base); if (!r.ok) return lp_fail(); }
        return r;
    }
    default: return lp_fail();                        /* function of var: not polynomial */
    }
}

/* ---- E2: denominator clearing ---------------------------------------------------------------
 * Rewrite so the target never sits in a denominator. v = d/t becomes v*t - d = 0.
 * Applied to the whole lhs-rhs expression: collect every denominator that mentions var, multiply
 * through by it. One pass handles the single-denominator case, which is every formula in the probe
 * set; nested cases fall through to !nosol rather than looping. */
static node_t *find_var_denominator(const node_t *n, const char *var, int depth) {
    if (!n || depth > MAX_DEPTH) return NULL;
    if (n->t == N_DIV && mentions(n->kid[1], var, 0)) return n->kid[1];
    for (int i = 0; i < n->nkid; i++) {
        node_t *d = find_var_denominator(n->kid[i], var, depth + 1);
        if (d) return d;
    }
    return NULL;
}

/* Structural equality, local copy -- deriv.c's is static. */
static int leq(const node_t *x, const node_t *y) {
    if (!x || !y || x->t != y->t || x->nkid != y->nkid) return 0;
    if (x->t == N_NUM) return x->num == y->num;
    if ((x->t == N_SYM || x->t == N_CALL) && strcmp(x->name, y->name)) return 0;
    for (int i = 0; i < x->nkid; i++) if (!leq(x->kid[i], y->kid[i])) return 0;
    return 1;
}

/* Return n*D with the multiplication DISTRIBUTED over +/- and cancelled against any (X/D) term.
 *
 * The shared simplifier deliberately does not distribute -- distribution can blow expressions up and
 * derivatives do not need it. But E2 does: multiplying v - d/t by t only clears the denominator if
 * the product is pushed down to the individual terms, where (d/t)*t cancels to d. Doing it here
 * keeps the risk contained to this transform. */
static node_t *mul_through(arena_t *a, const node_t *n, const node_t *D, int depth) {
    if (!n || !D || depth > MAX_DEPTH) return NULL;
    if (n->t == N_ADD || n->t == N_SUB) {
        node_t *l = mul_through(a, n->kid[0], D, depth + 1);
        node_t *r = mul_through(a, n->kid[1], D, depth + 1);
        if (!l || !r) return NULL;
        return ar_bin(a, n->t, l, r);
    }
    if (n->t == N_NEG) {
        node_t *k = mul_through(a, n->kid[0], D, depth + 1);
        return k ? sym_neg(a, k) : NULL;
    }
    if (n->t == N_DIV && leq(n->kid[1], D)) return ar_clone(a, n->kid[0]);   /* (X/D)*D = X */
    node_t *cn = ar_clone(a, n), *cd = ar_clone(a, D);
    if (!cn || !cd) return NULL;
    return ar_bin(a, N_MUL, cn, cd);
}

err_t literal_solve(arena_t *a, const node_t *eq, const char *var, char *out, size_t sz) {
    /* Build lhs - rhs (or take the bare expression as already equal to zero). */
    node_t *expr;
    if (eq->t == N_EQ) {
        node_t *l = ar_clone(a, eq->kid[0]);
        node_t *r = ar_clone(a, eq->kid[1]);
        if (!l || !r) return E_RANGE;
        expr = ar_bin(a, N_SUB, l, r);
    } else {
        expr = ar_clone(a, eq);
    }
    if (!expr) return E_RANGE;
    if (!mentions(expr, var, 0)) return E_NOSOL;

    /* E2: clear a denominator containing var, at most twice (v=d/t needs one; a=(v-u)/t one). */
    for (int pass = 0; pass < 2; pass++) {
        node_t *den = find_var_denominator(expr, var, 0);
        if (!den) break;
        expr = mul_through(a, expr, den, 0);
        if (!expr) return E_RANGE;
        if (simplify(a, expr, &expr) != E_NONE) return E_RANGE;
    }
    if (find_var_denominator(expr, var, 0)) return E_NOSOL;

    lpoly_t p = lextract(a, expr, var, 0);
    if (!p.ok) return E_NOSOL;

    node_t *A = p.c[2], *B = p.c[1], *C = p.c[0];

    node_t *res = NULL;
    if (!A && B) {
        /* E1: A*t + B = 0  ->  t = -C/B  (C is the constant term) */
        node_t *num = C ? sym_neg(a, C) : ar_num(a, 0);
        res = ar_bin(a, N_DIV, num, B);
    } else if (A && !B) {
        /* E3r: A*t^2 + C = 0  ->  t = +-sqrt(-C/A) */
        node_t *num = C ? sym_neg(a, C) : ar_num(a, 0);
        node_t *rad = ar_bin(a, N_DIV, num, A);
        node_t *sq  = ar_new(a, N_CALL);
        if (!sq || !rad) return E_RANGE;
        strcpy(sq->name, "sqrt");
        sq->kid[0] = rad; sq->nkid = 1;
        res = sq;
    } else if (A && B) {
        return E_NOSOL;                                /* full quadratic literal: out of scope */
    } else {
        return E_NOSOL;                                /* var vanished */
    }
    if (!res) return E_RANGE;

    if (simplify(a, res, &res) != E_NONE) return E_RANGE;
    if (canon(a, res, var, &res) != E_NONE)  return E_RANGE;

    char body[MAX_RESULT];
    err_t e = render(res, var, body, sizeof body);
    if (e) return e;

    /* E3r yields a +- pair; E1 a single value. */
    int pm = (A && !B);
    size_t need = strlen(var) + 1 + strlen(body) + (pm ? 4 : 0) + 1;
    if (need > sz) return E_RANGE;
    if (pm) snprintf(out, sz, "%s=+-%s", var, body);
    else    snprintf(out, sz, "%s=%s",   var, body);
    return E_NONE;
}
