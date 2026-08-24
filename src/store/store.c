#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "store.h"

/* Lowercase word-boundary substring match. The device has no regex and no std::map; this is the
 * whole matching primitive and it is what the training scorer used. */
static int has_term(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = hay; *p; p++) {
        if (p != hay && (isalnum((unsigned char)p[-1]))) continue;
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n && !isalnum((unsigned char)p[n])) return 1;
    }
    return 0;
}

/* Score in integer milli-units: matched terms * 1000 / total terms. Mirrors fit_score()'s
 * |q & r| / |r| exactly, without floating point. */
static int score_milli(const ns_record *r, const char *q) {
    if (r->nterms == 0) return 0;
    int hit = 0;
    for (int i = 0; i < r->nterms; i++) if (has_term(q, r->terms[i])) hit++;
    return hit * 1000 / r->nterms;
}

const ns_record *ns_retrieve(const ns_store *s, const char *question, int *fit_high) {
    int best = -1, best_i = -1, second = -1;
    for (int i = 0; i < s->n; i++) {
        int sc = score_milli(&s->rec[i], question);
        if (sc > best) { second = best; best = sc; best_i = i; }
        else if (sc > second) second = sc;
    }
    /* No term matched at all -> no record, not the first one. `best` starts at -1, so a score of
     * 0 compared greater and silently returned record 0 for an unrelated question. */
    if (best_i < 0 || best <= 0) { if (fit_high) *fit_high = 0; return 0; }
    /* Band on the absolute score, as fit-v1 does. The margin is retained for a later refinement
     * and must move on BOTH sides in one change if it ever does. */
    if (fit_high) *fit_high = (best >= NS_FIT_THRESHOLD_MILLI);
    (void)second;
    return &s->rec[best_i];
}

/* A variable is supplied if the question binds it ("v = 12") or the record carries a constant. */
const char *ns_missing_var(const ns_record *r, const char *question) {
    for (int i = 0; i < r->nvars; i++) {
        /* The left-hand side is the OUTPUT. Requiring it as an input made every well-formed
         * question report its own answer as missing -- caught by the standalone test before a
         * single device round-trip. */
        if (r->lhs && !strcmp(r->var[i], r->lhs)) continue;
        if (r->cval[i]) continue;
        if (has_term(question, r->var[i])) continue;
        return r->var[i];
    }
    return 0;
}
