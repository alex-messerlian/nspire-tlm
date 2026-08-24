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
    /* v2 bands on MATCH COUNTS, not on the ratio. score_milli is retained only to order records. */
    int best = -1, best_i = -1, second = -1, best_hits = 0, second_hits = 0;
    for (int i = 0; i < s->n; i++) {
        int hits = 0;
        for (int k = 0; k < s->rec[i].nterms; k++)
            if (has_term(question, s->rec[i].terms[k])) hits++;
        int sc = score_milli(&s->rec[i], question);
        if (sc > best) { second = best; second_hits = best_hits;
                         best = sc; best_hits = hits; best_i = i; }
        else if (sc > second) { second = sc; second_hits = hits; }
    }
    /* No term matched at all -> no record, not the first one. `best` starts at -1, so a score of
     * 0 compared greater and silently returned record 0 for an unrelated question. */
    if (best_i < 0 || best <= 0) { if (fit_high) *fit_high = 0; return 0; }
    /* fit-v2, and it must match corpus/refusal.py band_v2() exactly -- a shipped scorer that bands
     * differently from the training scorer teaches one rule and applies another. */
    {   const ns_record *r = &s->rec[best_i];
        int need = r->nterms < NS_FIT_MIN_MATCH ? r->nterms : NS_FIT_MIN_MATCH;
        if (fit_high) *fit_high = (best_hits >= need && best_hits > second_hits);
    }
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
