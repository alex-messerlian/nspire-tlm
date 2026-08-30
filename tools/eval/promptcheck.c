/* Vet candidate example prompts BEFORE they go on screen.
 *
 * The empty screen used to carry suggestion rows and they were REMOVED, because they ellipsised at
 * this width -- "A car goes 150 m in 12 s. Find ..." -- so the one thing they existed to do, show
 * what a question looks like, was the one thing they could not do. A candidate is only usable if
 * BOTH hold, and both are measurable:
 *
 *   1. it fits the pane at F_SM with no ellipsis
 *   2. the shortlist actually finds its record, so a student who taps it sees the tool work
 *   3. THE ASSEMBLED PROMPT IS A QUESTION. The first set failed exactly here and nothing caught
 *      it: "hooke's law, k = 250, x = 0.08" assembles to <q>hooke's law k = 250, x = 0.08.</q>,
 *      which names a relation and asks for nothing. The model refused correctly and it read on
 *      the device as a model failure. Checks 1 and 2 are structural; this one reads the document.
 *
 * Prompt text on stdin, one per line, as `prompt<TAB>expected_formula`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/store/pickui.h"
#include "../../src/store/gfx.h"

static ns_store2 ST;
static const char *nrm(const char *f, char *b, int cap) {
    int o = 0; for (const char *c = f; *c && o < cap-1; c++) if (*c!='('&&*c!=')'&&*c!=' ') b[o++]=*c;
    b[o]=0; return b;
}
int main(int argc, char **argv) {
    int maxw = argc > 2 ? atoi(argv[2]) : 268;      /* pane width less padding */
    if (ns_load(&ST, argc > 1 ? argv[1] : "build/store.tns") != NS_OK) return 2;
    gfx_init();
    char line[512]; int bad = 0;
    printf("%-42s %5s %5s %-4s %5s %5s  %s\n", "prompt", "px", "fits", "rank", "shape", "asks", "verdict");
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        char *tab = strchr(line, '\t'); if (!tab) continue;
        *tab = 0;
        const char *q = line, *want = tab + 1;
        int w = gfx_text_w(q, F_SM);
        static pk_state P; pk_open(&P, &ST, q);
        int rank = -1; char b[256];
        for (int i = 0; i < P.nsug; i++) if (!strcmp(nrm(ST.rec[P.sug[i]].formula, b, sizeof b), want)) { rank = i+1; break; }
        /* The assembled prompt must carry a lead-in frame -- the form 99.96% of training
         * questions with values use -- and an interrogative or imperative verb. */
        static char prompt[NS_PROMPT_MAX];
        static ns_ask a; ask_build(&ST, q, &a);
        int shape = 0, asks = 0;
        if (P.nsug && ns_assemble(prompt, sizeof prompt, &ST.rec[P.sug[rank>0?rank-1:0]],
                                  a.question, &a.in) > 0) {
            shape = strstr(prompt, ". Given ") || strstr(prompt, "? Given ") ? 1 : 0;
            static const char *V[] = {"find","calculate","compute","determine","what","how",
                                      "estimate","give","state"};
            for (unsigned i = 0; i < sizeof V / sizeof V[0]; i++) {
                for (const char *h = a.question; *h; h++) {
                    unsigned k = 0;
                    while (V[i][k] && h[k] &&
                           ((h[k]>='A'&&h[k]<='Z' ? h[k]-'A'+'a' : h[k]) == V[i][k])) k++;
                    if (!V[i][k]) { asks = 1; break; }
                }
                if (asks) break;
            }
        }
        int ok = (w <= maxw) && rank > 0 && shape && asks;
        if (!ok) bad++;
        printf("%-42s %5d %5s %-4s %5s %5s  %s\n", q, w, w <= maxw ? "yes" : "NO",
               rank > 0 ? (char[4]){'#', (char)('0'+rank), 0, 0} : "MISS",
               shape ? "yes" : "NO", asks ? "yes" : "NO",
               ok ? "ok" : !asks ? "ASKS FOR NOTHING" : !shape ? "not the training shape"
                    : (w > maxw ? "TOO WIDE" : "not in the shortlist"));
    }
    printf("\n%d unusable\n", bad);
    return bad ? 1 : 0;
}
