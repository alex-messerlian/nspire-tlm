/* Pick a record for each question on stdin. Prints: formula<TAB>score<TAB>nvals
 * argv[2] = "novars" runs the picker WITHOUT the supplied-variable term -- the control, in the
 * same binary as the subject, so the comparison cannot be between two implementations. */
#include <stdio.h>
#include <string.h>
#include "../../src/store/askparse.h"
int main(int argc, char **argv) {
    ns_store2 st;
    int use_vars = !(argc > 2 && !strcmp(argv[2], "novars"));
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
