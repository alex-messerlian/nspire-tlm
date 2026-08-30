#include <string.h>
#include "askparse.h"

static int lc(char c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }
static int alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int idch(char c) { return alpha(c) || (c >= '0' && c <= '9') || c == '_'; }

/* Does `word` (already lowercase, NUL-terminated) appear in `hay` as a WHOLE WORD?
 *
 * A substring scan is what shipped first, and it picked a virtual-particle record for
 * "what is force": that record's name is "Approximate time FOR exchange of a virtual particle",
 * and "for" is a substring of "force". Both ends have to sit on a boundary. Eleventh instance of
 * the proxy-predicate pattern in this repo -- "contains the letters" standing in for "uses the
 * word" -- and, as every previous instance, it was the control that caught it and not the rule. */
static int word_in(const char *hay, const char *word) {
    for (const char *h = hay; *h; h++) {
        if (h != hay && alpha(h[-1])) continue;
        int k = 0;
        while (word[k] && h[k] && lc(h[k]) == word[k]) k++;
        if (word[k]) continue;
        if (alpha(h[k])) continue;
        return 1;
    }
    return 0;
}

int ask_pick(const ns_store2 *st, const char *question, const ns_input *in, int use_vars,
             int *score_out) {
    int idx = 0, best = 0;
    if (!st || !question) { if (score_out) *score_out = 0; return 0; }
    for (int r = 0; r < st->n; r++) {
        const char *nm = st->rec[r].name;
        if (!nm || !nm[0]) continue;
        int score = 0;
        char word[48]; int w = 0;
        for (const char *c = nm; ; c++) {
            if (*c && alpha(*c) && w < (int)sizeof word - 1) {
                word[w++] = (char)lc(*c);
            } else {
                if (w >= 3) { word[w] = 0; if (word_in(question, word)) score++; }
                w = 0;
                if (!*c) break;
            }
        }
        /* SUPPLIED VARIABLES. A student who types "k = 500, x = 0.4" has named two of the three
         * variables of F=-k*x; no wording of the question carries that much information about
         * which record is meant. The LHS is skipped -- it is the OUTPUT, and a question that
         * supplied it would not be asking. */
        if (use_vars && in) {
            for (int v = 0; v < in->nvals; v++) {
                for (int k = 0; k < st->rec[r].nvars; k++) {
                    if (st->rec[r].lhs && !strcmp(st->rec[r].var[k], st->rec[r].lhs)) continue;
                    if (!strcmp(st->rec[r].var[k], in->var[v])) { score++; break; }
                }
            }
        }
        if (score > best) { best = score; idx = r; }
    }
    if (score_out) *score_out = best;
    return idx;
}

/* Scan one `= <number>` starting at `p`. Returns the end of the number, or NULL if there isn't
 * one. Shared by the parser and the stripper so they cannot disagree about what a value is --
 * they did disagree in the first draft, and the question kept a fragment the parser had taken. */
static const char *eat_assign(const char *p) {
    while (*p == ' ') p++;
    if (*p != '=') return 0;
    p++;
    while (*p == ' ') p++;
    if (*p == '-' || *p == '+') p++;
    int digits = 0;
    while ((*p >= '0' && *p <= '9') || *p == '.') { if (*p != '.') digits++; p++; }
    if (!digits) return 0;
    if (*p == 'e' || *p == 'E') {                       /* 3.0e8, 3.71e-07 */
        const char *save = p; p++;
        if (*p == '-' || *p == '+') p++;
        int ed = 0; while (*p >= '0' && *p <= '9') { ed++; p++; }
        if (!ed) p = save;
    }
    return p;
}

void ask_parse(const char *question, ns_ask *a) {
    a->in.nvals = 0;
    int vn = 0;
    for (const char *c = question; *c && a->in.nvals < NS_MAX_VARS2; ) {
        if (!(alpha(*c) || *c == '_')) { c++; continue; }
        const char *ns = c;
        while (idch(*c)) c++;
        int nlen = (int)(c - ns);
        const char *vs = c;
        while (*vs == ' ') vs++;
        if (*vs != '=') continue;                       /* not an assignment; resume after the word */
        vs++;
        while (*vs == ' ') vs++;
        const char *end = eat_assign(c);
        if (!end) { c = vs; continue; }
        int vlen = (int)(end - vs);
        if (vn + nlen + vlen + 2 >= (int)sizeof a->slab) break;
        char *nd = a->slab + vn; memcpy(nd, ns, (size_t)nlen); nd[nlen] = 0; vn += nlen + 1;
        char *vd = a->slab + vn; memcpy(vd, vs, (size_t)vlen); vd[vlen] = 0; vn += vlen + 1;
        a->in.var[a->in.nvals] = nd; a->in.val[a->in.nvals] = vd; a->in.nvals++;
        c = end;
    }
}

/* ns_assemble APPENDS the given list to the question -- that is the training format -- so a
 * question that still carries the values inline comes out with everything twice:
 *   <q>what is force, k = 500, x = 0.4 k = 500, x = 0.4.</q>
 * Strip the spans and the punctuation they leave behind, keep the prose. */
static void ask_strip(const char *question, ns_ask *a) {
    int o = 0;
    for (const char *c = question; *c && o < (int)sizeof a->qstrip - 1; ) {
        if (alpha(*c) || *c == '_') {
            const char *k = c;
            while (idch(*k)) k++;
            const char *end = eat_assign(k);
            if (end) {
                while (*end == ' ' || *end == ',') end++;
                while (o && (a->qstrip[o-1] == ' ' || a->qstrip[o-1] == ',')) o--;
                if (o && *end) a->qstrip[o++] = ' ';
                c = end;
                continue;
            }
            while (c < k && o < (int)sizeof a->qstrip - 1) a->qstrip[o++] = *c++;
            continue;
        }
        a->qstrip[o++] = *c++;
    }
    while (o && (a->qstrip[o-1] == ' ' || a->qstrip[o-1] == ',')) o--;
    a->qstrip[o] = 0;
}

void ask_build(const ns_store2 *st, const char *question, ns_ask *a) {
    ask_parse(question, a);                        /* values first -- the picker scores on them */
    a->idx = ask_pick(st, question, &a->in, 1, &a->score);
    a->question = question;
    if (a->in.nvals) {
        ask_strip(question, a);
        if (a->qstrip[0]) a->question = a->qstrip;
    }
}
