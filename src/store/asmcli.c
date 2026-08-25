/* CLI over the device assembler, so the host end-to-end test exercises the SHIPPED C path rather
 * than a Python re-implementation. Reads "rid<TAB>question<TAB>var=val,var=val" lines; an empty
 * rid means no record was picked and takes the Form C path. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "assemble.h"
#include "picker.h"
int main(int argc, char **argv) {
    ns_store2 st;
    int rc = ns_load(&st, argc > 1 ? argv[1] : "build/store.tns");
    if (rc != NS_OK) { fprintf(stderr, "load: %s\n", ns_strerror(rc)); return 2; }
    static char line[4096], out[NS_PROMPT_MAX];
    while (fgets(line, sizeof line, stdin)) {
        char *p = line, *rid = strsep(&p, "\t"), *q = strsep(&p, "\t"), *vals = p ? p : (char*)"";
        if (!rid || !q) continue;
        size_t L = strlen(vals); while (L && (vals[L-1]=='\n' || vals[L-1]=='\r')) vals[--L] = '\0';
        if (!rid[0]) { ns_assemble_none(out, sizeof out, q); puts(out); continue; }
        int idx = -1;
        for (int i = 0; i < st.n; i++) if (!strcmp(st.rec[i].rid, rid)) { idx = i; break; }
        if (idx < 0) { puts("!norecord"); continue; }
        ns_input in; in.nvals = 0;
        for (char *tok = strtok(vals, ","); tok && in.nvals < NS_MAX_VARS2; tok = strtok(NULL, ",")) {
            char *eq = strchr(tok, '=');
            if (!eq) continue;
            *eq = '\0'; in.var[in.nvals] = tok; in.val[in.nvals] = eq + 1; in.nvals++;
        }
        if (ns_assemble(out, sizeof out, &st.rec[idx], q, &in) < 0) puts("!overflow");
        else puts(out);
    }
    ns_free(&st);
    return 0;
}
