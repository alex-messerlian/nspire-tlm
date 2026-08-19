/* integrate.c -- adaptive Simpson quadrature.
 *
 * Hard caps on both recursion depth and total integrand evaluations. A model will eventually ask for
 * the integral of 1/x across zero, and on a device with no preemption an unbounded refinement loop
 * means the calculator is dead until someone pulls the battery. Bounded and wrong (E_RANGE) beats
 * unbounded and right.
 */
#include "eval.h"
#include <math.h>
#include <string.h>

typedef struct {
    const node_t *f;
    binds_t       b;
    int           evals;
    err_t         err;
} ictx;

static double fx(ictx *c, double x) {
    if (c->err) return 0;
    if (++c->evals > MAX_INTEG_EVALS) { c->err = E_RANGE; return 0; }
    c->b.val[0] = x;
    quant_t q;
    err_t e = num_eval(c->f, &c->b, 0, &q);
    if (e) { c->err = e; return 0; }
    if (!dim_is_scalar(q.d)) { c->err = E_UNITS; return 0; }
    return q.v;
}

static double simpson(double fa, double fm, double fb, double h) {
    return h / 6.0 * (fa + 4.0 * fm + fb);
}

static double adapt(ictx *c, double a, double b, double fa, double fm, double fb,
                    double whole, double eps, int depth) {
    if (c->err) return 0;
    double m  = 0.5 * (a + b);
    double lm = 0.5 * (a + m), rm = 0.5 * (m + b);
    double flm = fx(c, lm), frm = fx(c, rm);
    if (c->err) return 0;

    double left  = simpson(fa, flm, fm, m - a);
    double right = simpson(fm, frm, fb, b - m);
    double delta = left + right - whole;

    if (depth >= 24 || fabs(delta) <= 15.0 * eps)
        return left + right + delta / 15.0;

    return adapt(c, a, m, fa, flm, fm, left,  eps / 2, depth + 1) +
           adapt(c, m, b, fm, frm, fb, right, eps / 2, depth + 1);
}

err_t integrate(const node_t *f, const char *var, double lo, double hi, double *out) {
    if (isnan(lo) || isnan(hi) || isinf(lo) || isinf(hi)) return E_DOMAIN;
    if (lo == hi) { *out = 0; return E_NONE; }

    int flip = 0;
    if (lo > hi) { double t = lo; lo = hi; hi = t; flip = 1; }

    ictx c;
    memset(&c, 0, sizeof c);
    c.f = f;
    c.b.name[0] = var;
    c.b.val[0]  = 0;
    c.b.n = 1;

    double fa = fx(&c, lo), fb = fx(&c, hi), fm = fx(&c, 0.5 * (lo + hi));
    if (c.err) return c.err;

    double whole = simpson(fa, fm, fb, hi - lo);
    double v = adapt(&c, lo, hi, fa, fm, fb, whole, 1e-10, 0);
    if (c.err) return c.err;
    if (isnan(v)) return E_DOMAIN;
    if (isinf(v)) return E_RANGE;

    *out = flip ? -v : v;
    return E_NONE;
}
