/* The prompt app_request sends, for a question and the record the student picked.
 * Reads "rid<TAB>question" lines on stdin; prints one prompt per line, or "ERR <reason>" (not a
 * bang-prefixed code: those are the evaluator's refusal codes, and this is not the evaluator).
 *
 * Same calls in the same order as device_app.c's app_request: ask_build (parse the givens, strip
 * them from the question) then ns_assemble on the record named by rid. The only thing app_request
 * does that this does not is prepend earlier turns, and a single-question eval item has none.
 * Exists so a host evaluation scores the prompt the device builds rather than the split's text,
 * which is a different shape (docs/RESULT_CORRECTNESS.md).
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/askparse.h"
int main(int argc, char **argv) {
    static ns_store2 st;
    if (ns_load(&st, argc > 1 ? argv[1] : "build/store.tns") != NS_OK) return 2;
    static char line[4096], out[NS_PROMPT_MAX];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        char *tab = strchr(line, '\t');
        if (!tab) { puts("ERR badline"); continue; }
        *tab = 0;
        const char *rid = line, *q = tab + 1;
        int idx = -1;
        for (int i = 0; i < st.n; i++)
            if (st.rec[i].rid && !strcmp(st.rec[i].rid, rid)) { idx = i; break; }
        if (idx < 0) { puts("ERR norecord"); continue; }
        static ns_ask a;
        ask_build(&st, q, &a);
        if (ns_assemble(out, sizeof out, &st.rec[idx], a.question, &a.in) < 0) puts("ERR overflow");
        else puts(out);
    }
    return 0;
}
