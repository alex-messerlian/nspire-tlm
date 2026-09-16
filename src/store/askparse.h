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

/* Pull `name = value` out of free text: k = 500, x = 0.4, c = 3.0e8, v = -12. */
void ask_parse(const char *question, ns_ask *a);

/* One call: pick, parse, strip. a->question is what to hand ns_assemble. */
void ask_build(const ns_store2 *st, const char *question, ns_ask *a);
#endif
void ask_fuzz_set(int on);
/* A67 name-coverage bonus: 1 shipping, 0 off, 2 shuffled control. Subject and control, one binary. */
void ask_cov_set(int m);
