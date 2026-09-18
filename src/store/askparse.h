#ifndef NS_ASKPARSE_H
#define NS_ASKPARSE_H
#include "loader.h"
#include "assemble.h"

/* Turning what a student TYPED into the three things ns_assemble needs: which record, which
 * givens, and the question with the givens taken back out.
 *
 * THIS LIVES IN ITS OWN TRANSLATION UNIT BECAUSE IT COULD NOT BE TESTED WHERE IT WAS BORN.
 * It began inside app_request() in device_app.c, which pulls in libndls and only cross-compiles,
 * so the only way to check it was to push a .tns and read the screen. I checked it instead by
 * re-implementing the same logic in Python -- which is the second-implementation trap this repo
 * has paid for eight times in corpus/build_splits.py alone, and it duly reported one real defect
 * and told me nothing about the C that would actually run. Extracted, it compiles on the host and
 * tools/eval/test_askparse.c drives the SHIPPED code against the SHIPPED store. */

typedef struct {
    int      idx;                 /* chosen record; 0 when nothing matched                       */
    int      score;               /* name-terms matched. score == 0 means the fallback fired      */
    ns_input in;                  /* givens; pointers into slab                                  */
    char     slab[NS_PROMPT_MAX];
    char     qstrip[NS_PROMPT_MAX];
    const char *question;         /* qstrip when values were stripped, else the caller's pointer  */
} ns_ask;

/* Score every record against the question: one point per WHOLE-WORD name term found, plus one
 * per SUPPLIED VARIABLE the record actually uses. Name words shorter than 3 characters are skipped
 * ("of", "a", "the" match everything). Ties go to the first record.
 *
 * The variable term is what makes "what is force, k = 500, x = 0.4" reach F=-k*x. On names alone
 * it scored one point on "force" and landed on "mass from net force and acceleration" -- a real
 * match, and the wrong record. `use_vars` is 0 only for the negative control in test_askparse;
 * the device always passes 1. */
int  ask_pick(const ns_store2 *st, const char *question, const ns_input *in, int use_vars,
              int *score_out);

/* Rank ALL records and return the best `k` into out[], best first; returns how many were filled.
 * The picker needs a SHORTLIST, not a winner: at retrieval@1 the ranker is wrong nine times in
 * ten, but a handful of suggestions at the top of the family list is useful at a far lower bar.
 * `mode` selects the scorer so a control and its subject are the SAME BINARY -- see ask_mode. */
enum { ASK_PLAIN = 0,      /* one point per whole-word name term, plus supplied variables */
       ASK_IDF   = 1,      /* the same, weighted by how RARE the term is across record names */
       ASK_QTY   = 2,      /* IDF, plus the QUANTITY words its variables' units imply */
       ASK_QSHUF = 3,      /* CONTROL: the same, with the unit->family association BROKEN */
       ASK_NOUN  = 4,      /* QTY, plus the student-facing NOUN each variable's unit implies */
       ASK_NSHUF = 5 };    /* CONTROL for the nouns: same words, association broken */
int  ask_rank(const ns_store2 *st, const char *question, const ns_input *in, int mode,
              int *out, int k);

/* The same ranking, with the SCORES carried out alongside the indices. Needed because a no-match
 * decision cannot be made from the winner alone: the raw top-1 score is unnormalised (a longer
 * question scores higher) and measured, the best single cut point on it refuses 49.2% of
 * out-of-scope questions while keeping only 68.0% of in-scope ones. Anything better has to compare
 * the top candidate against its rivals, which means seeing more than one score.
 * `k` is clamped to ASK_RANK_MAX; every caller in the tree asks for 32 or fewer. */
#define ASK_RANK_MAX 32

/* Of the QUESTION's content words, the percentage this record's name accounts for (0..100).
 * The A67 coverage bonus measures the other direction (how much of the NAME the question matched);
 * a no-match decision needs this one, because what makes a question out-of-scope is how much of it
 * goes unexplained. See the comment on the definition for the measurement that forced it. */
int  ask_qcover(const ns_store2 *st, int r, const char *question);

/* IS THE TOP RECORD GOOD ENOUGH TO USE WITHOUT ASKING THE STUDENT? Fills *idx_out with the top
 * record (-1 if nothing scored) and returns 1 when the question is well enough explained.
 *
 * THIS IS NOT A REFUSAL TEST, and the distinction is the whole design. ask_pick has no way to say
 * "no match" -- it opens `int idx = 0` and returns record 0, which is Hooke's law -- and a cut
 * point on its SCORE cannot fix that: measured over 2,000 certified out-of-scope questions, the
 * best single threshold on the top-1 score refuses 49.2% of them while keeping 68.0% of in-scope
 * questions. The score is unnormalised, so a long out-of-scope question and a long physics word
 * problem look alike. Every rule built on it that refused out-of-scope well also refused word
 * problems: normalising by question length reached 98.0% refusal while keeping 5.8% of word
 * problems, which is a length filter wearing a discriminator's name.
 *
 * What separates them is how much of the QUESTION the winner explains (ask_qcover). Measured on
 * the shipped 1,606-record store at this threshold:
 *
 *     record-name lookups     100.0% confident  (150)
 *     glossary lookups         99.7% confident  (300)
 *     physics word problems     6.8% confident  (103)   <- and THOSE are 71.4% correct
 *     certified out-of-scope    2.2% confident  (2000)     against 28.1% for the rest
 *
 * So a lookup is answered directly and a word problem goes to the picker, which is right: at
 * retrieval@1 of 31.1% on word problems the student's choice is worth far more than the ranker's.
 * A false negative costs one picker screen -- today's behaviour for every question -- so this can
 * only reduce the number of screens, never refuse something the device could answer.
 *
 * THE THRESHOLD SITS ON A PLATEAU, not on a fitted point: 67 and 75 give identical rates on all
 * four populations, and 50 admits 41.7% of word problems. 75 is chosen inside that plateau because
 * it is the minimum over the six device-transcript questions, which is stated plainly rather than
 * buried -- the plateau is chosen by measurement, the point within it by the demo. */
#define ASK_CONFIDENT_MIN 75
int  ask_confident(const ns_store2 *st, const char *question, const ns_input *in, int *idx_out);
int  ask_rank_scored(const ns_store2 *st, const char *question, const ns_input *in, int mode,
                     int *out, int *scores, int k);

/* Pull `name = value` out of free text: k = 500, x = 0.4, c = 3.0e8, v = -12. */
void ask_parse(const char *question, ns_ask *a);

/* One call: pick, parse, strip. a->question is what to hand ns_assemble. */
void ask_build(const ns_store2 *st, const char *question, ns_ask *a);
#endif
void ask_fuzz_set(int on);
/* A67 name-coverage bonus: 1 shipping, 0 off, 2 shuffled control. Subject and control, one binary. */
void ask_cov_set(int m);
