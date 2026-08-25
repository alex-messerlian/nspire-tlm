/* CLI wrapper so provenance.c is reachable from the scoring path.
 * Before this existed provenance.c had no main and no caller: the invariant was written,
 * unit-tested by test_prov.c, and never run on a single model output. Reads one document on
 * stdin (NUL-free), prints "answer=<n> call=<n>" -- both 0 means clean. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int prov_unsourced(const char *doc, double *first);
int prov_call_unsourced(const char *doc, double *first);
int main(void) {
    static char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof(buf) - 1, stdin);
    buf[n] = '\0';
    double a = 0, c = 0;
    printf("answer=%d call=%d\n", prov_unsourced(buf, &a), prov_call_unsourced(buf, &c));
    return 0;
}
