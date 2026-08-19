/* stat.c -- list statistics.
 *
 * sd and var are SAMPLE statistics (n-1 denominator), per TOOL_SPEC.md section 3. This is the single
 * most common silent wrongness in a stats tool, so it is stated in the spec, restated here, and
 * pinned by a test.
 */
#include "eval.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* Accepts comma or whitespace separated numbers, with or without surrounding braces or brackets. */
static err_t parse_list(const char *s, double *v, int *n) {
    *n = 0;
    while (*s) {
        while (*s == ' ' || *s == ',' || *s == '{' || *s == '}' || *s == '[' || *s == ']' || *s == '\t') s++;
        if (!*s) break;
        char *end;
        double d = strtod(s, &end);
        if (end == s) return E_EXPR;
        if (*n >= MAX_LIST) return E_RANGE;
        if (isnan(d) || isinf(d)) return E_DOMAIN;
        v[(*n)++] = d;
        s = end;
    }
    return *n > 0 ? E_NONE : E_EXPR;
}

static int cmpd(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

err_t stat_op(const char *op, const char *list, double *out) {
    double v[MAX_LIST];
    int n;
    err_t e = parse_list(list, v, &n);
    if (e) return e;

    if (!strcmp(op, "n")) { *out = n; return E_NONE; }

    double sum = 0;
    for (int i = 0; i < n; i++) sum += v[i];

    if (!strcmp(op, "sum"))  { *out = sum; return E_NONE; }
    if (!strcmp(op, "mean")) { *out = sum / n; return E_NONE; }
    if (!strcmp(op, "min")) { double m = v[0]; for (int i=1;i<n;i++) if (v[i]<m) m=v[i]; *out=m; return E_NONE; }
    if (!strcmp(op, "max")) { double m = v[0]; for (int i=1;i<n;i++) if (v[i]>m) m=v[i]; *out=m; return E_NONE; }
    if (!strcmp(op, "median")) {
        qsort(v, (size_t)n, sizeof v[0], cmpd);
        *out = (n % 2) ? v[n/2] : 0.5 * (v[n/2 - 1] + v[n/2]);
        return E_NONE;
    }
    if (!strcmp(op, "var") || !strcmp(op, "sd")) {
        if (n < 2) return E_DOMAIN;                  /* sample variance is undefined for n=1 */
        double mean = sum / n, ss = 0;
        for (int i = 0; i < n; i++) { double dd = v[i] - mean; ss += dd * dd; }
        double var = ss / (n - 1);
        *out = !strcmp(op, "var") ? var : sqrt(var);
        return E_NONE;
    }
    return E_NAME;
}
