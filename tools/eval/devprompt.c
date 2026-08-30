/* The EXACT prompt app_request builds, for a question and a chosen relation.
 * Reproduces the device path: ask_build (pick + parse + strip) then ns_assemble.
 * usage: devprompt <store.tns> "<question>" [suggestion_index]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/store/pickui.h"
int main(int argc, char **argv) {
    ns_store2 st;
    if (ns_load(&st, argv[1]) != NS_OK) return 2;
    const char *q = argv[2];
    int pick = argc > 3 ? atoi(argv[3]) : 0;
    static pk_state P; pk_open(&P, &st, q);
    printf("shortlist:\n");
    for (int i = 0; i < P.nsug; i++)
        printf("  %d %-46s %s\n", i, st.rec[P.sug[i]].name, st.rec[P.sug[i]].formula);
    if (pick >= P.nsug) { printf("no such suggestion\n"); return 1; }
    static ns_ask a; ask_build(&st, q, &a);
    printf("\nquestion as sent: \"%s\"\n", a.question);
    printf("givens parsed:    %d", a.in.nvals);
    for (int i = 0; i < a.in.nvals; i++) printf("  %s=%s", a.in.var[i], a.in.val[i]);
    printf("\n\nPROMPT:\n");
    static char p[NS_PROMPT_MAX];
    if (ns_assemble(p, sizeof p, &st.rec[P.sug[pick]], a.question, &a.in) < 0) { printf("(overflow)\n"); return 1; }
    printf("%s\n", p);
    return 0;
}
