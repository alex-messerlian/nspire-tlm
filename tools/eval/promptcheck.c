/* Vet candidate example prompts BEFORE they go on screen.
 *
 * The empty screen used to carry suggestion rows and they were REMOVED, because they ellipsised at
 * this width -- "A car goes 150 m in 12 s. Find ..." -- so the one thing they existed to do, show
 * what a question looks like, was the one thing they could not do. A candidate is only usable if
 * BOTH hold, and both are measurable:
 *
 *   1. it fits the pane at F_SM with no ellipsis
 *   2. the shortlist actually finds its record, so a student who taps it sees the tool work
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
    printf("%-46s %5s %5s  %-4s %s\n", "prompt", "px", "fits", "rank", "verdict");
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        char *tab = strchr(line, '\t'); if (!tab) continue;
        *tab = 0;
        const char *q = line, *want = tab + 1;
        int w = gfx_text_w(q, F_SM);
        static pk_state P; pk_open(&P, &ST, q);
        int rank = -1; char b[256];
        for (int i = 0; i < P.nsug; i++) if (!strcmp(nrm(ST.rec[P.sug[i]].formula, b, sizeof b), want)) { rank = i+1; break; }
        int ok = (w <= maxw) && rank > 0;
        if (!ok) bad++;
        printf("%-46s %5d %5s  %-4s %s\n", q, w, w <= maxw ? "yes" : "NO",
               rank > 0 ? (char[4]){'#', (char)('0'+rank), 0, 0} : "MISS",
               ok ? "ok" : (w > maxw ? "TOO WIDE" : "not in the shortlist"));
    }
    printf("\n%d unusable\n", bad);
    return bad ? 1 : 0;
}
