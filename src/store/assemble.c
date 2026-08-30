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

static int appendn(char *out, int cap, int n, const char *s, int len) {
    if (n < 0) return -1;
    if (n + len >= cap) return -1;
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
    /* THE QUESTION IS CLOSED BEFORE THE GIVENS, and the givens get a LEAD-IN. Measured over the
     * 239,838-document corpus: of the 232,758 questions carrying a value, 99.96% use one of 16
     * lead-in frames, and the device used NONE -- it appended "  v = a, b = c." straight onto the
     * ask. Every question the calculator has ever built was in a surface form the model saw in
     * 0.04% of training, and those 0.04% are raw OpenStax artifacts, not the generated form.
     *
     * Sixth train/serve skew of this class, after units, condition, fit, missing and the <res>
     * span -- and the one nobody diffed, because gate_record_bytes compares the RECORD span and
     * this is the QUESTION span. gate_question_shape now covers it.
     *
     * The form reproduced here is corpus/generate.py compose_question()'s suffix branch:
     *     ask.rstrip(".?") + ("?" if it ended with ? else ".") + " " + frame + "."
     * which is 34.9% of the corpus. "Given" is one of the 16 frames, all near-uniform at ~6.3%.
     * Example, from the corpus: "Find V. Given q = 2.66e-07, U_E = 4.47e-05." */
    {   int qlen = (int)strlen(question);
        while (qlen > 0 && (question[qlen-1] == ' ' )) qlen--;
        int was_q = (qlen > 0 && question[qlen-1] == '?');
        while (qlen > 0 && (question[qlen-1] == '.' || question[qlen-1] == '?' ||
                            question[qlen-1] == ' ')) qlen--;
        n = appendn(out, cap, n, question, qlen);
        n = appends(out, cap, n, was_q ? "?" : ".");
    }
    /* THE ENTERED VALUES GO IN THE QUESTION. The training format carries givens there
     * ("<q>Given v = 3, d = 5, find ...</q>"), and provenance requires every number in a tool
     * call to trace to the question or the record. Without this the model has nothing to compute
     * with, invents its inputs, and provenance correctly rejects 89% of answers -- which is
     * exactly what the first end-to-end run measured. */
    /* THE RECORD'S CONSTANTS GO IN BESIDE THEM, and this file has claimed to do it since it was
     * written -- "constants are inlined", in the header comment above, with no code behind it.
     * DESIGN_PRINCIPLE.md carries the same claim as capability #4 at 97.5%.
     *
     * Three things break without it, and all three were measured (docs/RESULT_MGH_TRACE.md):
     *   1. The model is handed `U=m*g*h` with two of three values and `missing:none`. Dropping the
     *      third factor is the best available continuation of that prompt.
     *   2. prov_call_unsourced sources numbers to the question and the record ONLY, so a CORRECT
     *      answer citing 9.81 scores unsourced while the WRONG one scores clean -- the grader
     *      prefers the wrong answer on all 37 constant-bearing records.
     *   3. tlm_shape_check cannot run: with g unbound the runtime cannot say what the call should
     *      be, so it returns UNCHECKED for the right call and the wrong one alike.
     *
     * Emitted in the SAME `v = value` form as the entered values, because that is the one form the
     * question span already uses and the form provenance and the shape check both read. */
    int nwrote = 0;
    if ((in && in->nvals > 0) || r->nvars) {
        for (int i = 0; in && i < in->nvals; i++) {
            n = appends(out, cap, n, nwrote++ ? ", " : " Given ");
            n = appends(out, cap, n, in->var[i]);
            n = appends(out, cap, n, " = ");
            n = appends(out, cap, n, in->val[i]);
        }
        for (int k = 0; k < r->nvars; k++) {
            if (!r->cval[k] || !r->cval[k][0]) continue;
            if (value_for(in, r->var[k])) continue;   /* the student overrode it; theirs wins */
            n = appends(out, cap, n, nwrote++ ? ", " : " Given ");
            n = appends(out, cap, n, r->var[k]);
            n = appends(out, cap, n, " = ");
            n = appends(out, cap, n, r->cval[k]);
        }
        if (nwrote) n = appends(out, cap, n, ".");
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
    /* NOT given the lead-in treatment above. Form C carries no givens, so there is nothing to lead
     * in to -- and this exact string is MEASURED: 100.0% refused, 0.0% confident answers on 88 real
     * questions the store cannot serve. Changing a measured string for symmetry would invalidate
     * the measurement and buy nothing. */
    n = appends(out, cap, n, question);
    n = appends(out, cap, n, "</q><r>none | missing:none | no matching relation | fit:low<a>");
    return n;
}
