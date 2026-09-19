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
/* A56. FUZZY WORD MATCHING, BECAUSE STUDENTS MISTYPE AND THE KEYPAD IS AWKWARD.
 *
 * Measured on device: the user could not find an apostrophe, typed "hookes law", and the picker
 * returned LAW OF REFRACTION. A54 fixed that one word. The general case is every word: "hooks",
 * "huke", "hokes", "momentom", "accelaration", "freqency".
 *
 * Bounded Levenshtein with early exit. The budget scales with length because a one-edit window on a
 * four-letter word matches far too much ("mass"/"pass"/"mask"/"maps"), while an eight-letter word
 * can absorb two typos and stay unambiguous:
 *
 *     len <= 4   exact only      -- too short to relax safely
 *     len 5-7    1 edit
 *     len >= 8   2 edits
 *
 * THE COST IS FALSE MATCHES AND IT IS REAL, not hypothetical. Relaxing matching by merely allowing
 * a trailing 's' was measured to make 7 of 200 eval questions match a record where nothing had
 * matched before. This is a strictly larger relaxation, so the threshold above is a starting point
 * to be MEASURED in both directions (typo-injection for recall, the 200-item benchmark for false
 * matches), not a setting to assert. See docs/RESULT_FUZZY.md. */
static int edist_le(const char *a, const char *b, int max) {
    int la = 0, lb = 0;
    while (a[la]) la++;
    while (b[lb]) lb++;
    int d = la - lb; if (d < 0) d = -d;
    if (d > max) return 0;                       /* length alone rules it out */
    int prev[40], cur[40];
    if (lb > 38) return 0;
    for (int j = 0; j <= lb; j++) prev[j] = j;
    for (int i = 1; i <= la; i++) {
        cur[0] = i;
        int lo = i - max, hi = i + max, best = max + 1;
        if (lo < 1) lo = 1;
        if (hi > lb) hi = lb;
        for (int j = 1; j <= lb; j++) cur[j] = max + 1;   /* outside the band is unreachable */
        for (int j = lo; j <= hi; j++) {
            int sub = prev[j - 1] + (lc(a[i - 1]) == lc(b[j - 1]) ? 0 : 1);
            int del = prev[j] + 1, ins = cur[j - 1] + 1;
            int v = sub < del ? sub : del; if (ins < v) v = ins;
            cur[j] = v; if (v < best) best = v;
        }
        if (best > max) return 0;                /* whole row exceeded the budget: stop */
        for (int j = 0; j <= lb; j++) prev[j] = cur[j];
    }
    return prev[lb] <= max;
}

static int fuzz_budget(const char *w) {
    int n = 0; while (w[n]) n++;
    /* A95. MEASURED, not chosen. The user's requirement is that a hundred ways of misspelling
     * "Hooke's law" should still reach it, so a 102-spelling set was built across the real failure
     * modes (missing apostrophe, phonetic, doubled and dropped letters, keypad neighbours, case,
     * spacing, question framing) and four budget curves were run against THREE populations, because
     * a looser matcher trades spelling recall against false matches and one number cannot show that:
     *
     *      curve                        spelling   glossary@1   out-of-scope confident
     *      n<=4?0 : n<=7?1 : 2  (was)    93/102     273/300           2.2%
     *      n<=3?0 : n<=6?2 : 3           98/102     269/300           2.2%
     *      n<=4?0 : n<=6?2 : 3  (this)   98/102     270/300           2.4%
     *      n<=4?0 : n<=7?1 : 3           93/102     272/300           2.3%
     *
     * The failures it fixes are 2-edit typos in 6-letter words -- "hukes"/"hoocks"/"hoks" against
     * the name's "hookes" -- which a budget of 1 cannot bridge. The 4-character floor STAYS: below
     * it a word is as close to a different word as to itself, and most of the store's variable
     * names are one or two characters.
     *
     * The cost is real and is stated rather than netted out: three glossary terms of 300 stop being
     * retrieved, and false matching on 2,000 certified out-of-scope questions rises 0.2 points. */
    return n <= 4 ? 0 : (n <= 6 ? 2 : 3);
}

/* Walk the question's words and accept one within the edit budget of `word`. */
static int word_near(const char *hay, const char *word) {
    int max = fuzz_budget(word);
    if (max == 0) return 0;
    char qw[40];
    for (const char *h = hay; ; h++) {
        if (*h && (alpha(*h) || *h == '_')) {
            int n = 0; const char *k = h;
            while (*k && (alpha(*k) || *k == '_' || (*k >= '0' && *k <= '9')) && n < 39) qw[n++] = (char)lc(*k++);
            qw[n] = 0; h = k - 1;
            if (n >= 3 && edist_le(qw, word, max)) return 1;
            if (!*k) break;
        } else if (!*h) break;
    }
    return 0;
}

/* Strip spaces and parentheses so a store relation and a student's typing of it compare equal:
 * "P=(((V)^(2))/(R))" and "P = V^2/R" both reduce to "P=V^2/R". Returns the length written. */
static int norm_expr(const char *s, char *out, int cap) {
    int n = 0;
    if (!s || !out || cap <= 0) return 0;
    for (; *s && n < cap - 1; s++)
        if (*s != ' ' && *s != '\t' && *s != '(' && *s != ')') out[n++] = *s;
    out[n] = 0;
    return n;
}

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
        /* ASK VERBS. These name the REQUEST, not the subject: "explain hookes law" is a question
         * about Hooke's law, and "explain" is no more part of the subject than "the" is.
         *
         * WHERE THIS ACTUALLY BITES is ask_qcover, not the score. is_stopword filters the words
         * each_word yields, and the scorer walks the RECORD NAME while qcover walks the QUESTION,
         * so a word absent from every name can only change qcover. Checked rather than assumed:
         * across 164 compute names and 1,442 knowledge terms, every word below occurs in EXACTLY
         * ZERO of them, so the name side and the family-name side cannot move.
         *
         * "state" was in the candidate list and was REMOVED: it occurs in 10 knowledge terms
         * ("equation of state", "excited state", "ground state"), where it is a physics noun. The
         * list was written from a measurement of the store, not from a sense of which words feel
         * like verbs, which is how "state" would have gone in.
         *
         * Without these, "explain hokes law" covered 1 of 3 words (33%) and "find work when
         * f=12 d=2.5" covered 1 of 2 (50%), purely because "explain" and "find" can never match
         * anything -- so two questions the device answers correctly looked like poor matches. */
        "explain", "find", "calculate", "define", "compute", "determine", "describe",
        "give", "tell", "show", "how", "why", "does", "did", "much", "many",
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
            /* A54. A POSSESSIVE IN A RECORD NAME IS ALSO REACHABLE WITHOUT THE APOSTROPHE.
             *
             * "Hooke's law" splits here into "hooke" and "law" -- the lone "s" is below the
             * length-3 floor -- so a student who CANNOT FIND THE APOSTROPHE on the keypad and
             * types "hookes law" matched neither, and the picker returned LAW OF REFRACTION.
             * Measured on device: the Hooke's law demo was unreachable.
             *
             * So the NAME also offers "hookes". This is deliberately narrower than relaxing the
             * matcher to accept any trailing 's': that version was built and measured first, and
             * it made SEVEN of 200 eval questions falsely match a record where nothing matched
             * before ("A runner averages 5.5 m/s for 1320 s. How far?" -> P_ave=I_rms*V_rms),
             * because it also turned "seconds"/"lengths"/"cables" into matches. Retrieval@1 was
             * unchanged at 18.4% either way, so the plural half bought nothing and cost seven
             * confident wrong suggestions. This form touches possessives only.
             *
             * Both scorer and document-frequency count run through this same walk, so the extra
             * word is counted consistently by construction -- the invariant this function's
             * docstring already states. */
            if (w >= 3 && *c == '\'' && lc(c[1]) == 's' && !alpha(c[2])) {
                word[w] = 's'; word[w + 1] = 0;
                if (!is_stopword(word)) fn(word, ctx);
            }
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

/* A55. A WORD REPEATED IN ONE RECORD NAME WAS SCORED ONCE PER OCCURRENCE.
 *
 * "Law of refraction (Snell's law)" contains "law" TWICE, so a question saying "law" scored it 2
 * while "Hooke's law" -- which genuinely matched two distinct words -- also scored 2, and the tie
 * went to Snell. Measured: "what is hookes law" returned LAW OF REFRACTION from ask_pick.
 *
 * A record name is a bag of DISTINCT terms for retrieval purposes; repeating one is a property of
 * the phrasing, not evidence about the question. Dedupe is per record, reset by score_record. */
/* nterms/nmatch drive the COVERAGE bonus in score_record: see there for why a sum alone is the
 * wrong shape for a ranking over names of very different lengths. */
struct sctx { const char *q; int score; int mode; char seen[12][24]; int nseen;
              int nterms; int nmatch; };
/* A56. Fuzzy matching is the SHIPPING DEFAULT, and ask_fuzz_set(0) is the control. Subject and
 * control are the same binary, which is the discipline rankcli's own header states.
 *
 * A64. IT DEFAULTED TO OFF AND NOTHING TURNED IT ON, so A56 shipped disabled. The comment that
 * stood here said ask_fuzz_set() "is called by the measurement harness and by the device once a
 * threshold has been measured" -- the device call was deferred, and the commit message then
 * reported 80.6% retrieval with every word mistyped as though it were live. The only caller in the
 * entire tree was tools/eval/rankcli.c.
 *
 * Measured on the DEVICE path (ask_build -> ask_pick) with the default as it was:
 *
 *     "what is hookes law"  ->  theta_2=asin((n_1*sin(theta_1))/n_2)     Snell's law
 *
 * which is verbatim the failure the device test reported: "what is hookes law is totally wrong
 * suggested snell". The fix was measured, committed, documented and switched off.
 *
 * The default therefore lives where the shipping behaviour is, not in a call site somebody has to
 * remember. tools/eval/test_askparse.c asserts it through ask_build, the device's own entry point,
 * on the four questions from that device transcript. */
static int FUZZ_ON = 1;
void ask_fuzz_set(int on) { FUZZ_ON = on; }

/* A67 name-coverage: 1 shipping, 0 off, 2 the shuffled control. See score_record. */
static int COV_MODE = 1;
void ask_cov_set(int m) { COV_MODE = m; }

static void score_word(const char *w, void *v) {
    struct sctx *c = (struct sctx *)v;
    /* A dedupe hit is the SAME term again (A55), so it is neither a new term nor a new match. */
    for (int i = 0; i < c->nseen; i++) if (!strcmp(c->seen[i], w)) return;
    if (c->nseen < 12) {                       /* no stdio in device code -- bounded copy */
        char *d = c->seen[c->nseen]; int n = 0;
        while (w[n] && n < (int)sizeof c->seen[0] - 1) { d[n] = w[n]; n++; }
        d[n] = 0; c->nseen++;
    }
    if (!word_in(c->q, w)) {
        /* A FUZZY HIT IS WEAKER EVIDENCE THAN AN EXACT ONE and must not outrank it. Half weight:
         * two fuzzy matches still lose to two exact ones, and a fuzzy match only decides a record
         * that would otherwise have scored nothing. */
        if (!(FUZZ_ON && word_near(c->q, w))) { c->nterms++; return; }   /* a term, not a match */
        c->score += ((c->mode == ASK_IDF) ? idf16(w) : 16) / 2;
        c->nterms++; c->nmatch++;
        return;
    }
    c->score += (c->mode == ASK_IDF) ? idf16(w) : 16;
    c->nterms++; c->nmatch++;
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
    struct sctx c = { question, 0, qty ? ASK_IDF : mode, {{0}}, 0, 0, 0 };
    each_word(nm, score_word, &c);
    int name_score = c.score;      /* what the NAME earned, before qty/noun cues and variables */

    /* A94. THE STUDENT TYPED THE FORMULA. Until now the scorer read record NAMES and never the
     * relation itself, so the single most specific thing a student can offer was thrown away.
     *
     * MEASURED on the shipped store before this term:
     *   "solve F=m*a for a"      -> f=((d_i*d_o)/(d_o+d_i))   the thin lens equation, because
     *                               "solve" fuzzy-matches the name "(SOLVED version)"
     *   "isolate k in F=-k*x"    -> "isolated system", because "isolate" matches "isolated"
     *   "rearrange W=F*d for d"  -> nothing at all
     * The question VERB was outscoring the relation printed next to it.
     *
     * Normalisation drops spaces and parentheses only. The store writes P=(((V)^(2))/(R)) and a
     * student types P=V^2/R; both reduce to P=V^2/R. Case is NOT folded, because case is physics
     * here -- T against t, V against v.
     *
     * The bonus scales with the MATCHED LENGTH rather than being flat, so when two records'
     * normalised relations both appear in a question the longer and more specific one wins on its
     * own evidence instead of on a tie-break. */
    {
        char nf[160], nq[640];
        /* A RELATION, NOT A TERM. The knowledge tier packs a glossary entry as an ordinary record
         * whose `formula` field IS THE TERM -- "force", "pressure", "kinetic energy". Without the
         * '=' test this bonus fired on those: "what is force, k = 500, x = 0.4" matched the
         * glossary record "force" for +240 and beat F=-k*x. test_askparse went 40/40 to 33/40 in
         * one build and named every case. Requiring '=' is the same structural predicate
         * gate_spare_given uses to tell the two populations apart. */
        if (norm_expr(st->rec[r].formula, nf, (int)sizeof nf) >= 4 && strchr(nf, '=') &&
            norm_expr(question, nq, (int)sizeof nq) > 0 && strstr(nq, nf))
            c.score += 48 * (int)strlen(nf);

        /* A110. THE RIGHT-HAND SIDE ON ITS OWN, because a CALCULUS question never writes the '='.
         *
         * The bonus above needs the whole relation, and a C1/C2/C3 question asks about an
         * EXPRESSION: "what is the derivative of 0.5*m*(v)^(2) with respect to v". With no '=' it
         * earned nothing here, and the expression's fragments then fuzzy-matched short glossary
         * words. MEASURED on device: 0 of 40 derivative questions retrieved the right record, and
         * all 40 returned the same one -- `ether`. A rate of exactly 0% with a CONSTANT wrong
         * answer is a scorer defect, not a hard question, and the same question phrased as
         * "derivative of kinetic energy with respect to v" retrieved correctly all along. The
         * whole C1/C2/C3 tier -- 13,520 documents -- was unreachable through the device's own
         * retrieval.
         *
         * The glossary bug this sits beside cannot come back through it: a glossary record's
         * `formula` IS its term and carries no '=', so it has no right-hand side to match.
         *
         * AN OPERATOR IS REQUIRED, not merely length. Without it the RHS of `theta_r=theta_i` is a
         * bare identifier and would match any question mentioning that variable, which is the
         * fuzzy-noise failure this fixes, rearranged. */
        const char *eq = strchr(nf, '=');
        if (eq && norm_expr(question, nq, (int)sizeof nq) > 0) {
            const char *rhs = eq + 1;
            int has_op = 0;
            for (const char *q = rhs; *q; q++)
                if (*q == '*' || *q == '/' || *q == '+' || *q == '-' || *q == '^') has_op = 1;
            if (has_op && strlen(rhs) >= 3 && strstr(nq, rhs))
                c.score += 48 * (int)strlen(rhs);
        }
    }

    /* A67. NAME COVERAGE. The score above is a SUM over matched name words, so a long name that
     * merely CONTAINS the question outscores a name that IS the question:
     *
     *   "what is acceleration?"  ->  "Acceleration of center of mass of rolling object"
     *   "what is AC current?"    ->  "rms current"
     *
     * both measured on the 1,607-record store, and both wrong for the same reason. The record
     * named exactly "acceleration" matched 1 of 1 of its words; the rolling-object record matched
     * 1 of 7 and scored the same. A sum cannot tell those apart, because it never looks at what it
     * did NOT match.
     *
     * The bonus is proportional to the FRACTION of the record's own name the question accounted
     * for, so a fully-matched name is rewarded and a fully-matched name that is also SHORT is
     * rewarded most. It is added, not multiplied: multiplying would let coverage overturn a record
     * that matched more words in absolute terms, which is the opposite failure.
     *
     * Scaled to 16 per word, the unit every other term here uses. */
    if (c.nterms > 0 && c.nmatch > 0) {
        /* THE CONTROL LIVES IN THE SAME BINARY, which is the discipline this repo already applies
         * to fuzzy matching and to the topic-scoping experiment. COV_SHUFFLE replaces this
         * record's own name length with another record's, so every record still receives a bonus
         * drawn from the same distribution and ONLY THE ASSOCIATION between a record and its own
         * coverage is broken. If the measured gain survives that, the gain was "more points" and
         * not "the right record"; the size-matched-random-subset discipline, applied to a scorer.
         *
         *   ask_cov_set(1)  shipping
         *   ask_cov_set(0)  no coverage term at all
         *   ask_cov_set(2)  the control
         */
        int denom = c.nterms;
        if (COV_MODE == 2) {
            const char *onm = st->rec[(r + 7) % st->n].name;
            struct sctx c2 = { question, 0, c.mode, {{0}}, 0, 0, 0 };
            if (onm && onm[0]) each_word(onm, score_word, &c2);
            if (c2.nterms > 0) denom = c2.nterms;
        }
        /* PROPORTIONAL TO WHAT THE NAME EARNED, not a flat bonus, and the first version was flat.
         *
         * A flat `16 * 2 * nmatch / nterms` regressed "what is force, k = 500, x = 0.4" from
         * Hooke's law to DRAG FORCE: the question supplies k and x, which are two of Hooke's three
         * variables and the strongest evidence any question carries, while "Drag force" matched the
         * single generic word "force" at 1 of 2 coverage and collected a bonus big enough to pass
         * it. Covering a name made of common words is not evidence; the flat form could not tell
         * the difference because it ignored WHICH words were covered.
         *
         * Scaling by name_score carries the IDF weighting through: a record that earned little
         * from its name gets little coverage bonus, and an exact match on a rare name doubles. */
        /* QUADRATIC IN COVERAGE, and the linear form was measured wrong. Linear gives a 2-word
         * name that matched ONE generic word a half-weight bonus, which is what took
         * "what is force, k = 500, x = 0.4" from Hooke's law to DRAG FORCE: the question supplies
         * k and x, two of Hooke's three variables and the strongest evidence a question carries,
         * and "Drag force" passed it on the single word "force".
         *
         * Squaring makes partial coverage cheap and complete coverage full:
         *
         *   1 of 1  -> 1.00   "acceleration", the case this exists for
         *   3 of 4  -> 0.56   "Newton's second law" against "newtons second law"
         *   1 of 2  -> 0.25   "Drag force" on the word "force"
         *   1 of 7  -> 0.02   "Acceleration of center of mass of rolling object"
         */
        if (COV_MODE != 0 && name_score > 0 && denom > 0)
            c.score += (name_score * c.nmatch * c.nmatch) / (denom * denom);
    }
    if (qty) {
        c.mode = (mode == ASK_QSHUF) ? ASK_QSHUF : ASK_QTY;
        score_qty(st, r, question, &c);
        if (mode == ASK_NOUN || mode == ASK_NSHUF) { c.mode = mode; score_nouns(st, r, question, &c); }
    }
    if (use_vars && in) {
        int vmatch = 0, vfree = 0;
        for (int k = 0; k < st->rec[r].nvars; k++)
            if (!(st->rec[r].lhs && !strcmp(st->rec[r].var[k], st->rec[r].lhs))) vfree++;
        for (int v = 0; v < in->nvals; v++)
            for (int k = 0; k < st->rec[r].nvars; k++) {
                if (st->rec[r].lhs && !strcmp(st->rec[r].var[k], st->rec[r].lhs)) continue;
                if (!strcmp(st->rec[r].var[k], in->var[v])) { c.score += 16; vmatch++; break; }
            }
        /* VARIABLE COVERAGE, the same quadratic shape as name coverage and for the same reason.
         *
         * Without it the two signals were asymmetric: covering a record's NAME was rewarded and
         * accounting for all of its INPUTS was not. "what is force, k = 500, x = 0.4" supplies
         * both free variables of F=-k*x, which is the strongest evidence a question carries -- the
         * note above says so -- and Hooke's law still lost to "Drag force", which matched the one
         * generic word "force" and 0 of its 4 variables.
         *
         * A record whose inputs the question fully accounts for is very probably the record meant.
         * A record sharing one common word is not. Squaring keeps a single incidental variable
         * match cheap, exactly as it does for names. */
        if (COV_MODE != 0 && vfree > 0 && vmatch > 0)
            c.score += (16 * 2 * vmatch * vmatch) / (vfree * vfree);
    }
    return c.score;
}

/* QUESTION-SIDE COVERAGE: of the question's own content words, what percentage does this record's
 * name account for? 0..100.
 *
 * THIS IS THE OPPOSITE DIRECTION FROM THE A67 BONUS, and the difference is the whole point. A67
 * asks what fraction of the RECORD'S NAME the question matched, which is what stops a seven-word
 * name outranking the record named exactly "acceleration". It says nothing about how much of the
 * QUESTION went unexplained, and that is the quantity a no-match decision needs:
 *
 *   "what is hookes law"                              -> {hookes, law}, both in "Hooke's law"  100%
 *   "Find the gradient of f(x,y,z) ... at point P"    -> {find, gradient, point, ...}, and the
 *                                                        winner "critical point" explains one    ~14%
 *
 * Measured over four populations, the raw top-1 score cannot tell a physics WORD PROBLEM from an
 * out-of-scope question, because both are long and both share few words with a terse record name.
 * Every rule built on the score alone that refused out-of-scope well also refused word problems:
 * normalising by question length reached 98.0% refusal while keeping 5.8% of word problems, which
 * is not a discriminator, it is a length filter. Coverage is the feature that is about MEANING
 * rather than about size.
 *
 * Both walks are the shipped ones -- each_word for what a content word is, word_in for what a
 * match is -- so this cannot disagree with the scorer about either. */
/* The name's words as each_word YIELDS them, which is not the same as the name's characters.
 * A54 makes "Hooke's law" offer "hookes" as well as "hooke" and "law", and that variant is exactly
 * what rescues the typo "hokes": edist("hokes","hookes") is 1 and within budget, while
 * edist("hokes","hooke") is 2 and is not. Matching against the raw string throws the variant away,
 * which is why "explain hokes law" read 50% -- the scorer finds that record and my coverage said
 * half the question was unexplained. Same defect as the first version, one level in: not an
 * asymmetric MATCHER this time, an asymmetric WORD LIST. */
#define QCOV_MAXW 16
struct qnw { char w[QCOV_MAXW][48]; int n; };
static void qnw_add(const char *w, void *v) {
    struct qnw *q = (struct qnw *)v;
    if (q->n >= QCOV_MAXW) return;              /* bounded: no allocation in device code */
    int i = 0; while (w[i] && i < 47) { q->w[q->n][i] = w[i]; i++; }
    q->w[q->n][i] = 0; q->n++;
}

struct qcov { const struct qnw *nw; int total; int match; };
static void qcov_word(const char *w, void *v) {
    struct qcov *q = (struct qcov *)v;
    q->total += 2;                              /* 2 == one exact match; see the fuzzy branch */
    /* EXACT THEN FUZZY, THE MIRROR IMAGE OF score_word. The scorer tests each NAME word against
     * the question with word_in and then word_near; this tests each QUESTION word against the name
     * the same way. The first version used word_in alone and read 50% on "what is hookes law" --
     * {hookes, law} against the name "Hooke's law", where the apostrophe defeats a literal match
     * on "hookes". The scorer has never had that problem, because A54 makes the NAME offer
     * "hookes"; an asymmetric test threw that away and under-counted every possessive record.
     * Checked against the oracle before it was believed: D1's own six questions are what exposed
     * it, reading 50/100/33/100/50/100 where the first should plainly have been 100. */
    for (int i = 0; i < q->nw->n; i++) if (!strcmp(q->nw->w[i], w)) { q->match += 2; return; }
    if (!FUZZ_ON) return;
    int mx = fuzz_budget(w);
    if (mx <= 0) return;
    /* HALF WEIGHT FOR A FUZZY HIT, which is score_word's own rule and is load-bearing here.
     *
     * "Solve F=m*a for a." reduces to ONE content word, "solve", and the store contains a record
     * named "lens/mirror equation (SOLVED version)". One fuzzy hit over a denominator of one read
     * as 100% coverage -- a perfectly explained question -- and it is the thin lens equation
     * answering a question about Newton's second law. Seven of the eighteen word problems that
     * cleared a full-weight threshold were that exact shape, which is why coverage measured
     * INVERSELY correlated with retrieval@1 on that population (11.1% against 35.3%).
     *
     * A rate of 100% over a denominator of one is not a measurement. Weighting the fuzzy hit at
     * half is not a fudge to exclude it: it is what the scorer already believes about fuzzy
     * evidence, and applying it here removes an asymmetry rather than adding a rule. */
    for (int i = 0; i < q->nw->n; i++)
        if (edist_le(q->nw->w[i], w, mx)) { q->match += 1; return; }
}

int ask_qcover(const ns_store2 *st, int r, const char *question) {
    if (!st || r < 0 || r >= st->n || !question) return 0;
    const char *nm = st->rec[r].name;
    if (!nm || !nm[0]) return 0;
    struct qnw nw = { {{0}}, 0 };
    each_word(nm, qnw_add, &nw);
    struct qcov q = { &nw, 0, 0 };
    each_word(question, qcov_word, &q);
    if (q.total <= 0) return 0;
    return (100 * q.match) / q.total;
}

int ask_confident(const ns_store2 *st, const char *question, const ns_input *in, int *idx_out) {
    if (idx_out) *idx_out = -1;
    if (!st || !question) return 0;
    int out[2], sc[2];
    int n = ask_rank_scored(st, question, in, ASK_NOUN, out, sc, 2);
    if (n < 1) return 0;
    if (idx_out) *idx_out = out[0];
    return ask_qcover(st, out[0], question) >= ASK_CONFIDENT_MIN;
}

int ask_rank_scored(const ns_store2 *st, const char *question, const ns_input *in, int mode,
                    int *out, int *scores, int k) {
    if (!st || !question || !out || !scores || k <= 0) return 0;
    if (k > ASK_RANK_MAX) k = ASK_RANK_MAX;
    df_build(st);
    int n = 0;
    for (int r = 0; r < st->n; r++) {
        int sc = score_record(st, r, question, in, 1, mode);
        if (sc <= 0) continue;
        /* CARRYING the score instead of RE-DERIVING it. The previous form called score_record a
         * second time inside the insertion loop, once per comparison -- correct, and O(n*k) extra
         * scoring passes over a store that is now 1,606 records. Identical output either way;
         * score_record is a pure function of (st, r, question, in, mode). */
        int at = n;
        while (at > 0 && scores[at-1] < sc) at--;
        if (at >= k) continue;
        if (n < k) n++;
        for (int j = n - 1; j > at; j--) { out[j] = out[j-1]; scores[j] = scores[j-1]; }
        out[at] = r; scores[at] = sc;
    }
    return n;
}

int ask_rank(const ns_store2 *st, const char *question, const ns_input *in, int mode,
             int *out, int k) {
    int scores[ASK_RANK_MAX];
    return ask_rank_scored(st, question, in, mode, out, scores, k);
}

/* THE AUTO-PICK. ONE SCORER, and until A64 there were two.
 *
 * This function had its OWN inline word splitter -- `for (c = nm; ; c++) if (alpha(c))` -- while
 * the picker's shortlist went through score_record()/each_word(). Three consecutive retrieval
 * fixes landed in each_word and NONE of them reached here:
 *
 *   A54  a possessive is reachable without the apostrophe ("hookes" matches "Hooke's")
 *   A55  a repeated word scores once ("Law of refraction (Snell's law)" counted "law" twice)
 *   A56  bounded-Levenshtein fuzzy matching
 *
 * Measured on the shipped store, this exact function, before the change:
 *
 *     "what is hookes law"  ->  theta_2=asin((n_1*sin(theta_1))/n_2)   score 2   Snell's law
 *
 * which is verbatim what the device test reported. ask_rank, given the same question and the same
 * store, returns F=-k*x. Two rankers, one question, opposite answers -- the hazard docs/
 * WIRING_AUDIT.md already records for this app, here in the function ask_build calls.
 *
 * So the body is now an argmax over score_record, the same function ask_rank ranks with, in the
 * same ASK_NOUN mode pk_open uses. A fix to the scorer now reaches both by construction rather
 * than by somebody remembering. The signature is unchanged; the supplied-variable term that used
 * to live here is score_record's, at the same weight relative to a word match. */
int ask_pick(const ns_store2 *st, const char *question, const ns_input *in, int use_vars,
             int *score_out) {
    int idx = 0, best = 0;
    if (!st || !question) { if (score_out) *score_out = 0; return 0; }
    df_build(st);
    for (int r = 0; r < st->n; r++) {
        int score = score_record(st, r, question, in, use_vars, ASK_NOUN);
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

    /* A52. REMOVING THE VALUES LEAVES THE WORDS THAT INTRODUCED THEM DANGLING.
     *
     * "find the kinetic energy when you have m=900 and v=800" stripped to
     * "find the kinetic energy when you have and", and ns_assemble then appended a period, so the
     * model was handed "...when you have and." -- the exact phrasing a student types, made
     * ungrammatical by our own stripper. Found by running the planned device turns on the host
     * before the device session, not by any gate: no gate reads a question.
     *
     * Trailing FILLER words only, and the walk stops at the first content word, so a question that
     * legitimately ends in one of these is the only thing at risk and none of the ASK frames does.
     * Leading and interior text is never touched -- "when you have" mid-sentence is fine, it is
     * only a trailing run with nothing after it that reads as broken. */
    static const char *FILL[] = { "and", "with", "where", "when", "you", "have", "has", "given",
                                  "for", "at", "of", "if", "using", "take", "suppose", "are",
                                  "is", "to", "from", "in", "on", "by", "that", "we", "there" };
    for (;;) {
        while (o && (a->qstrip[o-1] == ' ' || a->qstrip[o-1] == ',')) o--;
        int e = o;
        while (e && idch(a->qstrip[e-1])) e--;          /* start of the trailing word */
        if (e == o) break;                              /* not a word -- punctuation, stop */
        int hit = 0;
        for (unsigned i = 0; i < sizeof FILL / sizeof FILL[0]; i++) {
            int L = (int)strlen(FILL[i]);
            if (o - e == L && !strncasecmp(a->qstrip + e, FILL[i], (size_t)L)) { hit = 1; break; }
        }
        if (!hit) break;
        o = e;
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
