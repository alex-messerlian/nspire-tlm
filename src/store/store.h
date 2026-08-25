/* Record store + retrieval scorer for the device.
 *
 * SHIPS THE TRAINING-TIME SCORER VERBATIM. A shipped scorer that bands differently from the one the
 * corpus was generated against teaches one rule and applies another, and both components would test
 * clean in isolation. Term overlap over record names is what the corpus was generated with, so it is what
 * runs here.
 *
 * The assembler emits three things the model is TRAINED to read and cannot function without:
 *   fit:high|low   band from the top-1 / top-2 comparative rule    (fit-v2, refusal.py)
 *   missing:X|none required variable absent from the question
 *   inlined constants  record-supplied values moved into the givens
 */
#ifndef NS_STORE_H
#define NS_STORE_H

/* fit-v2: comparative banding. No absolute threshold -- see corpus/refusal.py band_v2().
 * high iff matched >= min(2, nterms) AND matched > second-best matched. Monotone under term
 * enrichment, which the v1 ratio was not. */
#define NS_FIT_MIN_MATCH 2
#define NS_MAX_VARS   8
#define NS_MAX_TERMS  12

typedef struct {
    const char *formula;                /* "v=d/t"                                */
    const char *display;                /* human-readable, for the screen         */
    const char *cond;                   /* the req field                          */
    const char *terms[NS_MAX_TERMS];    /* retrieval terms                        */
    int         nterms;
    const char *lhs;                    /* the quantity SOLVED FOR -- never a required input */
    const char *var[NS_MAX_VARS];       /* variable names                         */
    const char *unit[NS_MAX_VARS];      /* matching units                         */
    const char *cval[NS_MAX_VARS];      /* supplied constant, or NULL             */
    int         nvars;
} ns_record;

typedef struct { const ns_record *rec; int n; } ns_store;

int  ns_store_load(ns_store *s, const char *path);
/* Returns the best record and writes the fit band (1 = high, 0 = low). */
const ns_record *ns_retrieve(const ns_store *s, const char *question, int *fit_high);
/* Writes "<q>...</q><r>FORMULA | VARS | missing:X | COND | fit:BAND" into out. */
int  ns_assemble(const ns_store *s, const char *question, char *out, int out_sz);
/* Exposed for the standalone test: which required variable is absent, or NULL. */
const char *ns_missing_var(const ns_record *r, const char *question);
#endif
