#include <string.h>
#include "askparse.h"
#include "picker.h"

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

/* Walk the words of `name`, lowercased, length >= 3. Returns via callback so the scorer and the
 * document-frequency count cannot disagree about what a word is -- they are the same walk. */
/* FUNCTION WORDS CARRY NO RETRIEVAL SIGNAL, WHATEVER THEIR DOCUMENT FREQUENCY.
 *
 * IDF assumes rarity implies informativeness. That holds for content words and fails badly here:
 * across 164 short record names "the" occurs in 3 and is therefore weighted 48 -- against 64 for
 * "kinetic" and 24 for "energy". So a question containing "the" was pulled hard toward the three
 * records whose names happen to contain it.
 *
 * Measured on a real user question: "find the kin energy when you have m=900 and v=800" returned
 * "electron energy in THE nth Bohr orbit" as top-1, beating "kinetic energy" -- because it matched
 * {energy, the} against {energy}. The same question WITHOUT the article ("kin energy m=900 v=800")
 * ranked kinetic energy first. A stopword decided it.
 *
 * The list is deliberately short: articles, conjunctions and bare prepositions. Anything that could
 * name or qualify a quantity stays in -- "per", "rate", "change" are all load-bearing here. */
static int is_stopword(const char *w) {
    static const char *STOP[] = {
        "the", "and", "for", "with", "from", "its", "this", "that", "are", "was",
        "into", "onto", "over", "under", "between", "due", "you", "your", "have",
        "when", "what", "which", "any", "all", "one", "two",
    };
    for (unsigned i = 0; i < sizeof STOP / sizeof STOP[0]; i++)
        if (!strcmp(STOP[i], w)) return 1;
    return 0;
}

static void each_word(const char *name, void (*fn)(const char *, void *), void *ctx) {
    char word[48]; int w = 0;
    for (const char *c = name; ; c++) {
        if (*c && alpha(*c) && w < (int)sizeof word - 1) { word[w++] = (char)lc(*c); }
        else {
            if (w >= 3) { word[w] = 0; if (!is_stopword(word)) fn(word, ctx); }
            w = 0;
            if (!*c) break;
        }
    }
}

/* DOCUMENT FREQUENCY over record names, built once. "force" appears in 20 names and "hooke" in
 * one, and the flat scorer counted them the same -- which is most of why retrieval@1 sat at 9.5%:
 * a question saying "force" matched twenty records at one point each and the tie went to
 * whichever came first in the store. */
#define ASK_DF_SLOTS 512
static struct { char w[24]; short df; } DF[ASK_DF_SLOTS];
static int DF_BUILT, DF_NREC;

static unsigned hashw(const char *s) {
    unsigned h = 2166136261u;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}
static short *df_slot(const char *w) {
    unsigned h = hashw(w) % ASK_DF_SLOTS;
    for (int probe = 0; probe < ASK_DF_SLOTS; probe++) {
        unsigned i = (h + (unsigned)probe) % ASK_DF_SLOTS;
        if (!DF[i].w[0]) {
            int n = 0; while (w[n] && n < (int)sizeof DF[i].w - 1) { DF[i].w[n] = w[n]; n++; }
            DF[i].w[n] = 0; return &DF[i].df;
        }
        if (!strcmp(DF[i].w, w)) return &DF[i].df;
    }
    return 0;                       /* table full: the word is simply unweighted */
}
static void df_bump(const char *w, void *ctx) { (void)ctx; short *s = df_slot(w); if (s) (*s)++; }

static void df_build(const ns_store2 *st) {
    if (DF_BUILT) return;
    memset(DF, 0, sizeof DF);
    for (int r = 0; r < st->n; r++) if (st->rec[r].name && st->rec[r].name[0])
        each_word(st->rec[r].name, df_bump, 0);
    DF_NREC = st->n; DF_BUILT = 1;
}

/* Integer stand-in for log(N/df), scaled by 16 so a common word is worth ~1 point and a unique
 * one ~8. No float: this runs on a device with no FPU and soft-float in the hot path is the
 * measured 16x penalty. */
static int idf16(const char *w) {
    short *s = df_slot(w);
    int df = (s && *s > 0) ? *s : 1;
    int ratio = DF_NREC / df;
    int bits = 0; while (ratio > 1) { ratio >>= 1; bits++; }   /* floor(log2(N/df)) */
    return 16 * (bits + 1) / 2;
}

struct sctx { const char *q; int score; int mode; };
static void score_word(const char *w, void *v) {
    struct sctx *c = (struct sctx *)v;
    if (!word_in(c->q, w)) return;
    c->score += (c->mode == ASK_IDF) ? idf16(w) : 16;
}

/* THE QUESTION NAMES THE PHYSICS; THE RECORD NAMES THE EPONYM.
 *
 * Measured on the 148 items.json questions whose gold record is still in the store: the record's
 * NAME shares a content word with the question 56.1% of the time. "A spring with k=250 N/m is
 * stretched 0.08 m. Find the force." against "Hooke's law" shares nothing -- and a student who
 * knew to type "Hooke" would not need the picker. No weighting scheme bridges that; the two are
 * different vocabularies.
 *
 * A record's VARIABLES carry the quantity its name omits. F is in newtons, newtons are family
 * "Force & pressure", and the question says "force". Reusing picker.c's unit->family map rather
 * than inventing a second taxonomy: it is already the thing that names quantities here, and a
 * new record joins it automatically. Ceiling for this cue alone: 62.8%; with the name, 68.2%. */
static void score_qty(const ns_store2 *st, int r, const char *question, struct sctx *c) {
    int seen[NS_MAX_FAMILIES]; int ns = 0;
    for (int k = 0; k < st->rec[r].nvars; k++) {
        int f = ns_family_of_unit(st->rec[r].unit[k]);
        if (f < 0) continue;
        int dup = 0;
        for (int j = 0; j < ns; j++) if (seen[j] == f) { dup = 1; break; }
        if (dup || ns >= NS_MAX_FAMILIES) continue;
        seen[ns++] = f;
        /* THE CONTROL. Same number of family names scored, same weights, same everything -- only
         * the unit->quantity association is broken, by a fixed rotation rather than an RNG so the
         * control is reproducible. If the gain survives this, it was never about units. */
        if (c->mode == ASK_QSHUF) f = (f + 7) % ns_family_count();
        /* The LHS's own family counts double: it is what the question is ASKING FOR, where the
         * other variables are only what it mentions. */
        int w = (st->rec[r].lhs && !strcmp(st->rec[r].var[k], st->rec[r].lhs)) ? 2 : 1;
        struct sctx q = { question, 0, ASK_PLAIN };
        each_word(ns_family_name(f), score_word, &q);
        c->score += w * q.score;
    }
}

/* THE STUDENT'S OWN NOUNS, one variable at a time.
 *
 * The record's NAME describes its left-hand side, and a student may ask for any variable in the
 * relation: v=d/t is named "average speed" and gets asked "how long does it take?". Scoring each
 * variable's unit nouns is what reaches those. The LHS still counts double -- it is what the
 * question is ASKING FOR, where the others are only what it mentions. */
static void score_nouns(const ns_store2 *st, int r, const char *question, struct sctx *c) {
    for (int k = 0; k < st->rec[r].nvars; k++) {
        const char *nouns = ns_unit_nouns(st->rec[r].unit[k]);
        if (!nouns) continue;
        /* THE LHS NOUN IS THE EVIDENCE; the others are noise at rank 1.
         *
         * Measured. At (LHS 2, other 1) the nouns bought recall and cost precision: @10 53.4 ->
         * 58.8 but @1 25.0 -> 22.3, because "energy", "time" and "distance" appear as INPUTS to
         * dozens of records, so every one of them rose on a question that merely mentioned them.
         * A question asks for ONE quantity, and that quantity is the left-hand side. */
        int is_lhs = (st->rec[r].lhs && !strcmp(st->rec[r].var[k], st->rec[r].lhs));
        if (!is_lhs) continue;
        int w = 3;
        char word[32]; int n = 0;
        for (const char *p = nouns; ; p++) {
            if (*p && *p != ' ' && n < (int)sizeof word - 1) { word[n++] = *p; }
            else {
                if (n >= 3) {
                    word[n] = 0;
                    /* CONTROL: same words, same weights, association broken by a fixed shift of
                     * the record index -- if the gain survives it was never about the nouns. */
                    const char *nn = nouns;
                    if (c->mode == ASK_NSHUF) {
                        nn = ns_unit_nouns(st->rec[(r + 41) % st->n].unit[0]);
                        if (!nn) { n = 0; if (!*p) break; else continue; }
                    }
                    if (nn == nouns && word_in(question, word)) c->score += w * 16;
                }
                n = 0;
                if (!*p) break;
            }
        }
        if (c->mode == ASK_NSHUF) {
            const char *nn = ns_unit_nouns(st->rec[(r + 41) % st->n].unit[0]);
            if (nn) {
                char w2[32]; int m2 = 0;
                for (const char *p = nn; ; p++) {
                    if (*p && *p != ' ' && m2 < (int)sizeof w2 - 1) w2[m2++] = *p;
                    else { if (m2 >= 3) { w2[m2] = 0; if (word_in(question, w2)) c->score += w * 16; }
                           m2 = 0; if (!*p) break; }
                }
            }
        }
    }
}

static int score_record(const ns_store2 *st, int r, const char *question,
                        const ns_input *in, int use_vars, int mode) {
    const char *nm = st->rec[r].name;
    if (!nm || !nm[0]) return 0;
    int qty = (mode == ASK_QTY || mode == ASK_QSHUF || mode == ASK_NOUN || mode == ASK_NSHUF);
    struct sctx c = { question, 0, qty ? ASK_IDF : mode };
    each_word(nm, score_word, &c);
    if (qty) {
        c.mode = (mode == ASK_QSHUF) ? ASK_QSHUF : ASK_QTY;
        score_qty(st, r, question, &c);
        if (mode == ASK_NOUN || mode == ASK_NSHUF) { c.mode = mode; score_nouns(st, r, question, &c); }
    }
    if (use_vars && in) {
        for (int v = 0; v < in->nvals; v++)
            for (int k = 0; k < st->rec[r].nvars; k++) {
                if (st->rec[r].lhs && !strcmp(st->rec[r].var[k], st->rec[r].lhs)) continue;
                if (!strcmp(st->rec[r].var[k], in->var[v])) { c.score += 16; break; }
            }
    }
    return c.score;
}

int ask_rank(const ns_store2 *st, const char *question, const ns_input *in, int mode,
             int *out, int k) {
    if (!st || !question || !out || k <= 0) return 0;
    df_build(st);
    int n = 0;
    for (int r = 0; r < st->n; r++) {
        int sc = score_record(st, r, question, in, 1, mode);
        if (sc <= 0) continue;
        int at = n;
        while (at > 0 && score_record(st, out[at-1], question, in, 1, mode) < sc) at--;
        if (at >= k) continue;
        if (n < k) n++;
        for (int j = n - 1; j > at; j--) out[j] = out[j-1];
        out[at] = r;
    }
    return n;
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
