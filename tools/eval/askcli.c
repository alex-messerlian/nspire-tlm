/* Pick a record for each question on stdin. Prints: formula<TAB>score<TAB>nvals
 * argv[2] = "novars" runs the picker WITHOUT the supplied-variable term -- the control, in the
 * same binary as the subject, so the comparison cannot be between two implementations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/store/askparse.h"
int main(int argc, char **argv) {
    ns_store2 st;
    int use_vars = !(argc > 2 && !strcmp(argv[2], "novars"));
    /* EXPLICIT, never implicit: argv[3]=0 disables fuzzy. A56 shipped disabled because a
     * default was doing the deciding and nothing said so. Default here is the SHIPPING
     * configuration, so this binary answers "what does the device do". */
    ask_fuzz_set(argc > 3 ? atoi(argv[3]) : 1);
    if (ns_load(&st, argc > 1 ? argv[1] : "build/store.tns") != NS_OK) return 2;
    char line[2048];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        static ns_ask a;
        ask_parse(line, &a);
        a.idx = ask_pick(&st, line, &a.in, use_vars, &a.score);
        printf("%s\t%d\t%d\n", st.rec[a.idx].formula ? st.rec[a.idx].formula : "", a.score, a.in.nvals);
    }
    return 0;
}
