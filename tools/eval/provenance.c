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
        /* A '-' is a SIGN only where a value can start. After a digit, a ')' or an identifier it is
         * the SUBTRACTION OPERATOR, and reading it as a sign made the subtrahend a negative literal
         * that matches nothing: `eval((26-8)/6)` against a question saying "8 m/s to 26 m/s" scanned
         * -8, reported it unsourced, and failed a correct call. Every relation with a subtraction
         * was affected -- 21 of 167 in the store, and the eval set's uniform-acceleration,
         * first-law and beat-frequency items among them.
         *
         * Found by making test_score.py's end-to-end control reachable: it had an early sys.exit
         * above it and had never run. */
        int sign_ok = 1;
        if (*p == '-' && p > s) {
            char prev = p[-1];
            if (isdigit((unsigned char)prev) || prev == ')' || prev == '.' ||
                isalpha((unsigned char)prev) || prev == '_') sign_ok = 0;
        }
        if (!isdigit((unsigned char)*p) && !(*p == '-' && sign_ok && isdigit((unsigned char)p[1]))) { p++; continue; }
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

/* Every numeric literal in a <tool> ARGUMENT must trace to the question or the record.
 *
 * Checking only the answer verifies the arithmetic and not the PREMISES: a model can invent its
 * inputs, the evaluator faithfully computes on them, and the answer then traces cleanly to a result
 * span the model manufactured itself. Observed for real -- a capacitance record and a question about
 * car speed produced <tool>eval<arg>(6.0)/(4.0)</tool>, with 6.0 and 4.0 appearing nowhere in the
 * question, and provenance reported 0 unsourced.
 *
 * Sources are the question and the record ONLY -- never a result span, because results come after
 * the call and cannot license its inputs. The record legitimately supplies constants (g=9.81) and
 * the formula's own literals (the 0.5 in 0.5*m*v^2), so both trace correctly. */
int prov_call_unsourced(const char *doc, double *first) {
    const char *qend = strstr(doc, "</q>");
    const char *aopen = strstr(doc, "<a>");
    if (!qend) return -1;
    const char *src_end = aopen ? aopen : doc + strlen(doc);
    const char *rec = qend + 4;

    double src[MAX_NUMS]; int ssig[MAX_NUMS];
    int ns = scan_nums(doc, qend, src, ssig, MAX_NUMS);          /* the question */
    {   /* the record: everything from </q> to the first <tool>, or to <a> if no call */
        const char *r_end = strstr(rec, "<tool>");
        if (!r_end || r_end > src_end) r_end = src_end;
        double r2[MAX_NUMS]; int s2[MAX_NUMS];
        int n2 = scan_nums(rec, r_end, r2, s2, MAX_NUMS);
        for (int i = 0; i < n2 && ns < MAX_NUMS; i++) { src[ns] = r2[i]; ssig[ns] = s2[i]; ns++; }
    }
    int bad = 0;
    for (const char *p = doc; (p = strstr(p, "<tool>")); ) {
        const char *e = strstr(p, "</tool>");
        if (!e) break;
        double a[MAX_NUMS]; int asig[MAX_NUMS];
        int na = scan_nums(p, e, a, asig, MAX_NUMS);
        for (int i = 0; i < na; i++) {
            int ok = 0;
            for (int j = 0; j < ns && !ok; j++)
                if (round_sig(src[j], asig[i]) == round_sig(a[i], asig[i])) ok = 1;
            if (!ok) { if (bad == 0 && first) *first = a[i]; bad++; }
        }
        p = e + 7;
    }
    return bad;
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
