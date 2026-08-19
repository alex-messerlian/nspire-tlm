/* deriv.c -- symbolic differentiation, simplification, and canonical rendering.
 *
 * Simplification is deliberately a small closed rule set applied to fixpoint with a hard pass cap.
 * A real CAS simplifier is unbounded work and unbounded runtime; this one is enough to make
 * derivative output readable and, more importantly, DETERMINISTIC, which TOOL_SPEC.md section 5.3
 * requires because training data is generated from these strings.
 */
#include "eval.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static int is_num(const node_t *n, double v) { return n && n->t == N_NUM && n->num == v; }
static int is_anynum(const node_t *n)        { return n && n->t == N_NUM; }

static int node_eq(const node_t *a, const node_t *b) {
    if (!a || !b || a->t != b->t || a->nkid != b->nkid) return 0;
    if (a->t == N_NUM) return a->num == b->num;
    if (a->t == N_SYM || a->t == N_CALL) if (strcmp(a->name, b->name)) return 0;
    for (int i = 0; i < a->nkid; i++) if (!node_eq(a->kid[i], b->kid[i])) return 0;
    return 1;
}

static node_t *neg_of(arena_t *a, node_t *x) {
    if (!x) return NULL;
    node_t *n = ar_new(a, N_NEG);
    if (!n) return NULL;
    n->kid[0] = x; n->nkid = 1;
    return n;
}
static node_t *call1(arena_t *a, const char *nm, node_t *x) {
    if (!x) return NULL;
    node_t *n = ar_new(a, N_CALL);
    if (!n) return NULL;
    strncpy(n->name, nm, MAX_IDENT - 1);
    n->kid[0] = x; n->nkid = 1;
    return n;
}

/* ---- differentiation ------------------------------------------------------------------------ */

static node_t *d(arena_t *a, const node_t *n, const char *var, err_t *err, int depth);

static node_t *d_call(arena_t *a, const node_t *n, const char *var, err_t *err, int depth) {
    node_t *u  = ar_clone(a, n->kid[0]);
    node_t *du = d(a, n->kid[0], var, err, depth + 1);
    if (*err) return NULL;
    if (!u || !du) { *err = E_RANGE; return NULL; }
    const char *f = n->name;

    if (!strcmp(f, "sin"))  return ar_bin(a, N_MUL, call1(a, "cos", u), du);
    if (!strcmp(f, "cos"))  return ar_bin(a, N_MUL, neg_of(a, call1(a, "sin", u)), du);
    if (!strcmp(f, "tan"))  return ar_bin(a, N_DIV, du,
                                   ar_bin(a, N_POW, call1(a, "cos", u), ar_num(a, 2)));
    if (!strcmp(f, "exp"))  return ar_bin(a, N_MUL, call1(a, "exp", u), du);
    if (!strcmp(f, "ln"))   return ar_bin(a, N_DIV, du, u);
    if (!strcmp(f, "log") || !strcmp(f, "log10"))
        return ar_bin(a, N_DIV, du, ar_bin(a, N_MUL, u, call1(a, "ln", ar_num(a, 10))));
    if (!strcmp(f, "sqrt")) return ar_bin(a, N_DIV, du,
                                   ar_bin(a, N_MUL, ar_num(a, 2), call1(a, "sqrt", u)));
    if (!strcmp(f, "sinh")) return ar_bin(a, N_MUL, call1(a, "cosh", u), du);
    if (!strcmp(f, "cosh")) return ar_bin(a, N_MUL, call1(a, "sinh", u), du);
    if (!strcmp(f, "tanh")) return ar_bin(a, N_DIV, du,
                                   ar_bin(a, N_POW, call1(a, "cosh", u), ar_num(a, 2)));
    if (!strcmp(f, "atan")) return ar_bin(a, N_DIV, du,
                                   ar_bin(a, N_ADD, ar_num(a, 1), ar_bin(a, N_POW, u, ar_num(a, 2))));
    if (!strcmp(f, "asin") || !strcmp(f, "acos")) {
        node_t *den = call1(a, "sqrt",
                        ar_bin(a, N_SUB, ar_num(a, 1), ar_bin(a, N_POW, u, ar_num(a, 2))));
        node_t *q = ar_bin(a, N_DIV, du, den);
        return !strcmp(f, "asin") ? q : neg_of(a, q);
    }
    /* abs, floor, ceil, round, min, max, fact: not differentiable, or not worth the edge cases.
     * Refuse cleanly -- the model can learn to route around !nosol. */
    *err = E_NOSOL;
    return NULL;
}

static node_t *d(arena_t *a, const node_t *n, const char *var, err_t *err, int depth) {
    if (*err) return NULL;
    if (depth > MAX_DEPTH) { *err = E_RANGE; return NULL; }
    if (!n) { *err = E_EXPR; return NULL; }

    switch (n->t) {
    case N_NUM: return ar_num(a, 0);
    case N_SYM: return ar_num(a, strcmp(n->name, var) == 0 ? 1 : 0);
    case N_NEG: return neg_of(a, d(a, n->kid[0], var, err, depth + 1));
    case N_ADD: case N_SUB: {
        node_t *l = d(a, n->kid[0], var, err, depth + 1);
        node_t *r = d(a, n->kid[1], var, err, depth + 1);
        if (*err) return NULL;
        return ar_bin(a, n->t, l, r);
    }
    case N_MUL: {
        node_t *u = ar_clone(a, n->kid[0]), *v = ar_clone(a, n->kid[1]);
        node_t *du = d(a, n->kid[0], var, err, depth + 1);
        node_t *dv = d(a, n->kid[1], var, err, depth + 1);
        if (*err) return NULL;
        return ar_bin(a, N_ADD, ar_bin(a, N_MUL, du, v), ar_bin(a, N_MUL, u, dv));
    }
    case N_DIV: {
        node_t *u = ar_clone(a, n->kid[0]), *v = ar_clone(a, n->kid[1]);
        node_t *v2 = ar_clone(a, n->kid[1]), *v3 = ar_clone(a, n->kid[1]);
        node_t *du = d(a, n->kid[0], var, err, depth + 1);
        node_t *dv = d(a, n->kid[1], var, err, depth + 1);
        if (*err) return NULL;
        node_t *num = ar_bin(a, N_SUB, ar_bin(a, N_MUL, du, v), ar_bin(a, N_MUL, u, dv));
        (void)v2;
        return ar_bin(a, N_DIV, num, ar_bin(a, N_POW, v3, ar_num(a, 2)));
    }
    case N_POW: {
        node_t *base = n->kid[0], *ex = n->kid[1];
        if (is_anynum(ex)) {                       /* power rule: n*u^(n-1)*u' */
            node_t *du = d(a, base, var, err, depth + 1);
            if (*err) return NULL;
            node_t *p = ar_bin(a, N_POW, ar_clone(a, base), ar_num(a, ex->num - 1));
            return ar_bin(a, N_MUL, ar_bin(a, N_MUL, ar_num(a, ex->num), p), du);
        }
        /* general: u^v * (v'*ln(u) + v*u'/u) */
        node_t *du = d(a, base, var, err, depth + 1);
        node_t *dv = d(a, ex,   var, err, depth + 1);
        if (*err) return NULL;
        node_t *t1 = ar_bin(a, N_MUL, dv, call1(a, "ln", ar_clone(a, base)));
        node_t *t2 = ar_bin(a, N_DIV, ar_bin(a, N_MUL, ar_clone(a, ex), du), ar_clone(a, base));
        return ar_bin(a, N_MUL, ar_clone(a, n), ar_bin(a, N_ADD, t1, t2));
    }
    case N_CALL: return d_call(a, n, var, err, depth);
    case N_EQ:   *err = E_EXPR; return NULL;
    }
    *err = E_EXPR;
    return NULL;
}

err_t deriv(arena_t *a, const node_t *n, const char *var, node_t **out) {
    err_t e = E_NONE;
    node_t *r = d(a, n, var, &e, 0);
    if (e) return e;
    if (!r) return E_RANGE;
    *out = r;
    return E_NONE;
}

/* ---- simplification ------------------------------------------------------------------------- */

static node_t *simp(arena_t *a, node_t *n, int *changed, int depth) {
    if (!n || depth > MAX_DEPTH) return n;
    for (int i = 0; i < n->nkid; i++) n->kid[i] = simp(a, n->kid[i], changed, depth + 1);

    node_t *l = n->nkid > 0 ? n->kid[0] : NULL;
    node_t *r = n->nkid > 1 ? n->kid[1] : NULL;

    switch (n->t) {
    case N_NEG:
        if (is_anynum(l))  { *changed = 1; return ar_num(a, -l->num); }
        if (l && l->t == N_NEG) { *changed = 1; return l->kid[0]; }
        break;
    case N_ADD:
        if (is_num(l, 0)) { *changed = 1; return r; }
        if (is_num(r, 0)) { *changed = 1; return l; }
        if (is_anynum(l) && is_anynum(r)) { *changed = 1; return ar_num(a, l->num + r->num); }
        if (r && r->t == N_NEG) { *changed = 1; return ar_bin(a, N_SUB, l, r->kid[0]); }
        break;
    case N_SUB:
        if (is_num(r, 0)) { *changed = 1; return l; }
        if (is_num(l, 0)) { *changed = 1; return neg_of(a, r); }
        if (is_anynum(l) && is_anynum(r)) { *changed = 1; return ar_num(a, l->num - r->num); }
        if (node_eq(l, r))                { *changed = 1; return ar_num(a, 0); }
        break;
    case N_MUL:
        if (is_num(l, 0) || is_num(r, 0)) { *changed = 1; return ar_num(a, 0); }
        if (is_num(l, 1)) { *changed = 1; return r; }
        if (is_num(r, 1)) { *changed = 1; return l; }
        if (is_anynum(l) && is_anynum(r)) { *changed = 1; return ar_num(a, l->num * r->num); }
        /* Push numeric factors left so "x*3" and "3*x" canonicalise to the same string. */
        if (is_anynum(r) && !is_anynum(l)) { *changed = 1; return ar_bin(a, N_MUL, r, l); }
        /* Then reassociate so they meet and fold: 3*(2*x) -> 6*x. Without this, the derivative of
         * 3x^2 renders as "3*2*x" instead of "6*x". */
        if (is_anynum(l) && r && r->t == N_MUL && is_anynum(r->kid[0])) {
            *changed = 1;
            return ar_bin(a, N_MUL, ar_num(a, l->num * r->kid[0]->num), r->kid[1]);
        }
        break;
    case N_DIV:
        if (is_num(r, 1)) { *changed = 1; return l; }
        if (is_num(l, 0)) { *changed = 1; return ar_num(a, 0); }
        if (is_anynum(l) && is_anynum(r) && r->num != 0) { *changed = 1; return ar_num(a, l->num / r->num); }
        if (node_eq(l, r) && !is_num(l, 0)) { *changed = 1; return ar_num(a, 1); }
        break;
    case N_POW:
        if (is_num(r, 1)) { *changed = 1; return l; }
        if (is_num(r, 0)) { *changed = 1; return ar_num(a, 1); }
        if (is_num(l, 1)) { *changed = 1; return ar_num(a, 1); }
        if (is_anynum(l) && is_anynum(r)) {
            double v = pow(l->num, r->num);
            if (!isnan(v) && !isinf(v)) { *changed = 1; return ar_num(a, v); }
        }
        break;
    default: break;
    }
    return n;
}

err_t simplify(arena_t *a, node_t *n, node_t **out) {
    for (int pass = 0; pass < MAX_SIMP_PASSES; pass++) {
        int changed = 0;
        n = simp(a, n, &changed, 0);
        if (!n) return E_RANGE;
        if (!changed) break;
    }
    *out = n;
    return E_NONE;
}

/* ---- canonical rendering (TOOL_SPEC.md section 5.3) ----------------------------------------- */

static int prec_of(const node_t *n) {
    switch (n->t) {
        case N_ADD: case N_SUB: return 1;
        case N_MUL: case N_DIV: return 2;
        case N_NEG:             return 2;
        case N_POW:             return 3;
        default:                return 4;
    }
}

static err_t emit(const node_t *n, char *out, size_t sz, size_t *k, int need, int depth);

static err_t put(char *out, size_t sz, size_t *k, const char *s) {
    size_t len = strlen(s);
    if (*k + len + 1 > sz) return E_RANGE;
    memcpy(out + *k, s, len);
    *k += len;
    out[*k] = 0;
    return E_NONE;
}

static err_t emit_kid(const node_t *n, char *out, size_t sz, size_t *k, int need, int depth) {
    int paren = prec_of(n) < need;
    err_t e;
    if (paren && (e = put(out, sz, k, "(")))  return e;
    if ((e = emit(n, out, sz, k, 0, depth + 1))) return e;
    if (paren && (e = put(out, sz, k, ")")))  return e;
    return E_NONE;
}

static err_t emit(const node_t *n, char *out, size_t sz, size_t *k, int need, int depth) {
    (void)need;
    if (!n) return E_EXPR;
    if (depth > MAX_DEPTH) return E_RANGE;
    err_t e;
    char buf[64];

    switch (n->t) {
    case N_NUM:
        if ((e = fmt_number(n->num, buf, sizeof buf))) return e;
        return put(out, sz, k, buf);
    case N_SYM:
        return put(out, sz, k, n->name);
    case N_NEG:
        if ((e = put(out, sz, k, "-"))) return e;
        return emit_kid(n->kid[0], out, sz, k, 3, depth);
    case N_ADD: case N_SUB:
        if ((e = emit_kid(n->kid[0], out, sz, k, 1, depth))) return e;
        if ((e = put(out, sz, k, n->t == N_ADD ? "+" : "-"))) return e;
        return emit_kid(n->kid[1], out, sz, k, n->t == N_ADD ? 1 : 2, depth);
    case N_MUL: case N_DIV:
        if ((e = emit_kid(n->kid[0], out, sz, k, 2, depth))) return e;
        if ((e = put(out, sz, k, n->t == N_MUL ? "*" : "/"))) return e;
        return emit_kid(n->kid[1], out, sz, k, n->t == N_MUL ? 2 : 3, depth);
    case N_POW:
        if ((e = emit_kid(n->kid[0], out, sz, k, 4, depth))) return e;
        if ((e = put(out, sz, k, "^"))) return e;
        return emit_kid(n->kid[1], out, sz, k, 3, depth);
    case N_CALL:
        if ((e = put(out, sz, k, n->name))) return e;
        if ((e = put(out, sz, k, "("))) return e;
        for (int i = 0; i < n->nkid; i++) {
            if (i && (e = put(out, sz, k, ","))) return e;
            if ((e = emit(n->kid[i], out, sz, k, 0, depth + 1))) return e;
        }
        return put(out, sz, k, ")");
    case N_EQ:
        if ((e = emit(n->kid[0], out, sz, k, 0, depth + 1))) return e;
        if ((e = put(out, sz, k, "="))) return e;
        return emit(n->kid[1], out, sz, k, 0, depth + 1);
    }
    return E_EXPR;
}

err_t render(const node_t *n, const char *var, char *out, size_t sz) {
    (void)var;
    size_t k = 0;
    out[0] = 0;
    return emit(n, out, sz, &k, 0, 0);
}
