/* provenance.c -- TOOL_SPEC section 8 invariant 2, enforced at the interface instead of the generator.
 *
 * "Every numeric claim in prose is backed by a preceding tool call in the same document."
 * That was written as a generator invariant. Inference is a producer no generator check covers:
 * nothing stops the model stating a number it never computed, which is the exact hallucination this
 * architecture exists to prevent. This makes the check runnable on the device, on the model's own
 * output, at answer time.
 *
 * A number in the answer is SOURCED if it matches -- at the answer's own precision -- some number in
 * the question, the record, or a result span. Rounding to the answer's significant figures is the
 * comparison, because "about 13 m/s" is a legitimate rendering of 12.5. */
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include "eval.h"

#define MAX_NUMS 128

static int scan_nums(const char *s, const char *end, double *out, int *sig, int max) {
    int n = 0;
    for (const char *p = s; p < end && n < max; ) {
        if (!isdigit((unsigned char)*p) && !(*p == '-' && isdigit((unsigned char)p[1]))) { p++; continue; }
        char *e; double v = strtod(p, &e);
        if (e == p) { p++; continue; }
        int d = 0, seen = 0;                       /* significant figures actually written */
        for (const char *q = p; q < e; q++) {
            if (*q == 'e' || *q == 'E') break;
            if (isdigit((unsigned char)*q)) { if (*q != '0' || seen) { d++; seen = 1; } }
        }
        out[n] = v; sig[n] = d < 1 ? 1 : d; n++;
        p = e;
    }
    return n;
}

static double round_sig(double v, int sig) {
    if (v == 0.0) return 0.0;
    double m = pow(10.0, (double)sig - 1.0 - floor(log10(fabs(v))));
    return floor(fabs(v) * m + 0.5) / m * (v < 0 ? -1.0 : 1.0);
}

/* doc: the full document. Returns count of UNSOURCED numbers in the answer span; 0 means clean.
 * If `first` is non-NULL, the first unsourced value is written there. */
int prov_unsourced(const char *doc, double *first) {
    /* <a> is the answer boundary in every document shape. Deriving it from the last </res>
     * instead fails on no-tool documents, where record and answer would run together -- which is
     * how the missing <a> was found in the first place. */
    const char *ans = strstr(doc, "<a>");
    if (!ans) return -1;                           /* malformed: no answer span */
    ans += 3;
    const char *aend = strstr(ans, "<end>"); if (!aend) aend = ans + strlen(ans);

    double src[MAX_NUMS]; int ssig[MAX_NUMS]; int ns = 0;
    /* sources: everything before the answer -- question, record, and every result span */
    ns = scan_nums(doc, ans, src, ssig, MAX_NUMS);

    double a[MAX_NUMS]; int asig[MAX_NUMS];
    int na = scan_nums(ans, aend, a, asig, MAX_NUMS);

    int bad = 0;
    for (int i = 0; i < na; i++) {
        int ok = 0;
        for (int j = 0; j < ns && !ok; j++)
            if (round_sig(src[j], asig[i]) == round_sig(a[i], asig[i])) ok = 1;
        if (!ok) { if (bad == 0 && first) *first = a[i]; bad++; }
    }
    return bad;
}
