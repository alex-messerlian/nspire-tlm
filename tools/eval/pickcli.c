/* Family coverage over the SHIPPED store, from the SHIPPED picker.
 *
 * "All 166 records map; no Other bucket" is load-bearing -- an unmapped record is unreachable by
 * browsing, which under E means unreachable at all. The store has since been cleaned to 164 and
 * several units corrected, so the claim is re-derived here rather than quoted from the spec. */
#include <stdio.h>
#include <string.h>
#include "../../src/store/pickui.h"   /* PK_ROWS -- the page size that SHIPS, not picker.h's estimate */
int main(int argc, char **argv) {
    ns_store2 st;
    if (ns_load(&st, argc > 1 ? argv[1] : "build/store.tns") != NS_OK) return 2;
    ns_family fam[NS_MAX_FAMILIES];
    int nf = ns_families(&st, fam, NS_MAX_FAMILIES);
    int mapped = 0, worst = 0;
    printf("%d records, %d families\n", st.n, nf);
    for (int f = 0; f < nf; f++) {
        mapped += fam[f].count;
        if (fam[f].count > worst) worst = fam[f].count;
        printf("  %-28s %3d%s\n", fam[f].name, fam[f].count, fam[f].count ? "" : "   <-- EMPTY");
    }
    printf("  mapped %d/%d   worst family %d records (%d screens of %d)\n",
           mapped, st.n, worst, (worst + PK_ROWS - 1) / PK_ROWS, PK_ROWS);
    if (mapped != st.n) {
        printf("\nUNMAPPED -- unreachable by browsing:\n");
        for (int i = 0; i < st.n; i++) if (ns_family_of(&st, i) < 0) {
            const char *lu = "?";
            for (int k = 0; k < st.rec[i].nvars; k++)
                if (st.rec[i].var[k] && !strcmp(st.rec[i].var[k], st.rec[i].lhs)) lu = st.rec[i].unit[k];
            printf("  %-34s lhs=%-10s unit=%s\n", st.rec[i].formula, st.rec[i].lhs, lu ? lu : "(none)");
        }
    }
    return mapped == st.n ? 0 : 1;
}
