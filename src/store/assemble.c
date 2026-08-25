/* Prompt assembly. See docs/PICKER_SPEC.md section 4.
 *
 * Under E only ONE thing is computed at inference: `missing:`. fit is high by construction,
 * units are looked up, constants are inlined. Everything the model would otherwise have to derive
 * is stated -- the design principle, applied at the last place it still applies. */
#include <stdio.h>
#include <string.h>
#include "assemble.h"

static int appends(char *out, int cap, int n, const char *s) {
    if (n < 0) return -1;
    int len = (int)strlen(s);
    if (n + len >= cap) return -1;          /* OVERFLOW IS -1, never a truncated prompt: a
                                             * silently cut record would change the physics */
    memcpy(out + n, s, (size_t)len);
    out[n + len] = '\0';
    return n + len;
}

static const char *value_for(const ns_input *in, const char *var) {
    if (!in) return 0;
    for (int i = 0; i < in->nvals; i++)
        if (in->var[i] && strcmp(in->var[i], var) == 0) return in->val[i];
    return 0;
}

int ns_assemble(char *out, int cap, const ns_rec2 *r, const char *question, const ns_input *in) {
    if (!out || !r || !question || cap <= 0) return -1;
    out[0] = '\0';
    int n = 0;
    n = appends(out, cap, n, "<q>");
    n = appends(out, cap, n, question);
    /* THE ENTERED VALUES GO IN THE QUESTION. The training format carries givens there
     * ("<q>Given v = 3, d = 5, find ...</q>"), and provenance requires every number in a tool
     * call to trace to the question or the record. Without this the model has nothing to compute
     * with, invents its inputs, and provenance correctly rejects 89% of answers -- which is
     * exactly what the first end-to-end run measured. */
    if (in && in->nvals > 0) {
        n = appends(out, cap, n, " ");
        for (int i = 0; i < in->nvals; i++) {
            if (i) n = appends(out, cap, n, ", ");
            n = appends(out, cap, n, in->var[i]);
            n = appends(out, cap, n, " = ");
            n = appends(out, cap, n, in->val[i]);
        }
        n = appends(out, cap, n, ".");
    }
    n = appends(out, cap, n, "</q><r>");
    n = appends(out, cap, n, r->formula);
    n = appends(out, cap, n, " | ");
    /* units for every variable, LHS included -- the answer needs a unit to state */
    for (int k = 0; k < r->nvars; k++) {
        if (k) n = appends(out, cap, n, " ");
        n = appends(out, cap, n, r->var[k]);
        n = appends(out, cap, n, ":");
        n = appends(out, cap, n, r->unit[k]);
    }
    /* missing: the ONLY inference-time computation. A required input with no value entered.
     * The LHS is what we are solving for and is never required as an input -- treating it as
     * one made every question look under-specified, which cost a device cycle in the first
     * store bring-up. */
    const char *miss = "none";
    for (int k = 0; k < r->nvars; k++) {
        if (strcmp(r->var[k], r->lhs) == 0) continue;
        if (r->cval[k] && r->cval[k][0]) continue;      /* supplied constant, not asked of the student */
        if (!value_for(in, r->var[k])) { miss = r->var[k]; break; }
    }
    n = appends(out, cap, n, " | missing:");
    n = appends(out, cap, n, miss);
    n = appends(out, cap, n, " | ");
    n = appends(out, cap, n, r->req && r->req[0] ? r->req : "standard conditions");
    /* NO <a> HERE. The training format is <q>..</q><r>RECORD<tool>..</tool><res>..</res><a>..
     * -- the model emits the tool call FIRST and opens <a> itself. Pre-opening <a> makes a tool
     * call impossible and every answer came back malformed. Form C DOES end with <a>, because
     * there is nothing to compute and the model goes straight to refusing. */
    n = appends(out, cap, n, " | fit:high");
    return n;
}

int ns_assemble_none(char *out, int cap, const char *question) {
    if (!out || !question || cap <= 0) return -1;
    out[0] = '\0';
    int n = 0;
    n = appends(out, cap, n, "<q>");
    n = appends(out, cap, n, question);
    n = appends(out, cap, n, "</q><r>none | missing:none | no matching relation | fit:low<a>");
    return n;
}
