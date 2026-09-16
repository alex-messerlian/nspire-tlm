/* Rank records for each question on stdin. Prints k formulas, best first, tab-separated.
 * argv[2] = mode (0 plain, 1 idf), argv[3] = k. Subject and control are the SAME BINARY. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/store/askparse.h"
int main(int argc, char **argv) {
    ns_store2 st;
    int mode = argc > 2 ? atoi(argv[2]) : 0, k = argc > 3 ? atoi(argv[3]) : 5;
    ask_fuzz_set(argc > 4 ? atoi(argv[4]) : 0);   /* A56: argv[4]=1 enables fuzzy */
    if (ns_load(&st, argc > 1 ? argv[1] : "build/store.tns") != NS_OK) return 2;
    char line[2048];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        static ns_ask a; ask_parse(line, &a);
        int out[32];
        int n = ask_rank(&st, line, &a.in, mode, out, k > 32 ? 32 : k);
        for (int i = 0; i < n; i++) printf("%s%s", i ? "\t" : "", st.rec[out[i]].formula);
        printf("\n");
    }
    return 0;
}
