/* Emit the RANKING SHAPE for each question on stdin, so a no-match rule can be chosen by
 * measurement rather than by taste.
 *
 * WHY NOT THE TOP-1 SCORE. ask_pick returns an argmax and nothing else, and the raw winning score
 * is unnormalised: a longer question matches more words and scores higher whether or not any
 * record fits. Measured over 2,000 certified out-of-scope questions against the in-scope eval
 * items, the best single cut point on it refuses 49.2% of out-of-scope while keeping 68.0% of
 * in-scope. That is close to useless, and it is a property of the statistic, not of the scorer.
 *
 * The shape of the whole ranking carries what the winner alone cannot: whether the best record
 * stands ABOVE its rivals. A real match beats the field; a question about nothing in the store
 * produces a flat pile of weak partial matches. Every column here is derived from scores the
 * shipped scorer already produces, so nothing is reimplemented.
 *
 * usage: nomatchcli <store.tns> [mode]      questions on stdin
 * prints: top1 top2 r3 r5 r8 median last ncand qcover   (tab separated, one row per question)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/store/askparse.h"

int main(int argc, char **argv) {
    ns_store2 st;
    if (ns_load(&st, argc > 1 ? argv[1] : "build/store.tns") != NS_OK) return 2;
    int mode = argc > 2 ? atoi(argv[2]) : ASK_NOUN;   /* the shipping scorer by default */
    char line[2048];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        static ns_ask a;
        ask_parse(line, &a);
        int out[ASK_RANK_MAX], sc[ASK_RANK_MAX];
        int n = ask_rank_scored(&st, line, &a.in, mode, out, sc, ASK_RANK_MAX);
        /* Several BACKGROUND definitions, because "how far above the field" needs a field and the
         * right rank to read it at is an empirical question, not an obvious one. Printing them all
         * costs nothing and lets one measurement choose. */
        int r3 = n > 2 ? sc[2] : 0, r5 = n > 4 ? sc[4] : 0;
        int r8 = n > 7 ? sc[7] : 0, last = n > 0 ? sc[n-1] : 0;
        int med = n > 0 ? sc[n/2] : 0;
        int qc = n > 0 ? ask_qcover(&st, out[0], line) : 0;
        printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
               n > 0 ? sc[0] : 0, n > 1 ? sc[1] : 0, r3, r5, r8, med, last, n, qc);
    }
    return 0;
}
