/* Session persistence: round-trip, and every way a file can be wrong.
 *
 * The interesting cases are not the happy path. A calculator loses power mid-write, and a file that
 * is half-written must load as NOTHING rather than as half a conversation -- because a sidebar that
 * silently lost the last three turns is a bug nobody reports.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../src/store/chatstore.h"

static int F;
static void T(const char *n, int got, int want) {
    int ok = got == want; if (!ok) F++;
    printf("  %s  %-50s got=%d want=%d\n", ok ? "PASS" : "FAIL", n, got, want);
}
static void TS(const char *n, const char *got, const char *want) {
    int ok = strcmp(got, want) == 0; if (!ok) F++;
    printf("  %s  %-50s \"%s\"\n", ok ? "PASS" : "FAIL", n, got);
    if (!ok) printf("        want \"%s\"\n", want);
}
static app_chat C[MAX_CHATS], L[MAX_CHATS];
static const char *P = "/tmp/tlm_chats_test.tns";

static void seed(void) {
    memset(C, 0, sizeof C);
    snprintf(C[0].title, sizeof C[0].title, "A car goes 150 m in 12 s");
    C[0].nturns = 2; C[0].used = 1;
    snprintf(C[0].turn[0].q,   sizeof C[0].turn[0].q,   "A car goes 150 m in 12 s. Find the speed.");
    snprintf(C[0].turn[0].a,   sizeof C[0].turn[0].a,   "<a>The speed is 12.5 m/s.<end>");
    snprintf(C[0].turn[0].sum, sizeof C[0].turn[0].sum, "v=d/t d=150 t=12 -> 12.5");
    C[0].turn[0].done = 1;
    snprintf(C[0].turn[1].q, sizeof C[0].turn[1].q, "");           /* an empty field must survive */
    snprintf(C[0].turn[1].a, sizeof C[0].turn[1].a, "<a>x<end>");
    C[0].turn[1].done = 0;
    snprintf(C[1].title, sizeof C[1].title, "3 A through a 4 ohm resistor");
    C[1].nturns = 1; C[1].used = 1;
    snprintf(C[1].turn[0].q, sizeof C[1].turn[0].q, "newlines\nand \"quotes\" and 12 34");
    snprintf(C[1].turn[0].a, sizeof C[1].turn[0].a, "<a>ok<end>");
}

int main(void) {
    int cur = -1;
    printf("\n  -- round trip --\n");
    seed();
    T("save succeeds", chat_save(P, C, 2, 1), 0);
    memset(L, 0, sizeof L);
    T("loads both sessions", chat_load(P, L, MAX_CHATS, &cur), 2);
    T("remembers the selection", cur, 1);
    TS("title survives", L[0].title, C[0].title);
    TS("question survives", L[0].turn[0].q, C[0].turn[0].q);
    TS("raw answer survives, markup intact", L[0].turn[0].a, C[0].turn[0].a);
    TS("compact summary survives", L[0].turn[0].sum, C[0].turn[0].sum);
    T("turn count", L[0].nturns, 2);
    T("done flag", L[0].turn[0].done, 1);
    T("not-done flag", L[0].turn[1].done, 0);
    TS("EMPTY field survives as empty", L[0].turn[1].q, "");
    TS("newlines and quotes survive", L[1].turn[0].q, C[1].turn[0].q);

    printf("\n  -- a file that is wrong must load as NOTHING --\n");
    T("missing file", chat_load("/tmp/tlm_does_not_exist.tns", L, MAX_CHATS, &cur), 0);
    T("  and reports no selection", cur, -1);

    /* truncation: chop the trailer, which is exactly what a power loss mid-write looks like */
    seed(); chat_save(P, C, 2, 0);
    { FILE *f = fopen(P, "rb"); fseek(f, 0, SEEK_END); long sz = ftell(f);
      char *buf = malloc((size_t)sz); rewind(f); fread(buf, 1, (size_t)sz, f); fclose(f);
      f = fopen(P, "wb"); fwrite(buf, 1, (size_t)sz - 8, f); fclose(f); free(buf); }
    T("truncated file loads nothing", chat_load(P, L, MAX_CHATS, &cur), 0);

    /* a byte count that lies */
    { FILE *f = fopen(P, "wb");
      fprintf(f, "TLMCHAT1\n1 0\n1\n9999\nshort\n1\n1\nq\n1\na\n1\ns\nENDTLM\n"); fclose(f); }
    T("lying byte count loads nothing", chat_load(P, L, MAX_CHATS, &cur), 0);

    /* wrong magic */
    { FILE *f = fopen(P, "wb"); fprintf(f, "NOPE\n1 0\nENDTLM\n"); fclose(f); }
    T("wrong magic loads nothing", chat_load(P, L, MAX_CHATS, &cur), 0);

    /* more sessions than the array holds */
    { FILE *f = fopen(P, "wb"); fprintf(f, "TLMCHAT1\n999 0\nENDTLM\n"); fclose(f); }
    T("absurd session count loads nothing", chat_load(P, L, MAX_CHATS, &cur), 0);

    /* empty file */
    { FILE *f = fopen(P, "wb"); fclose(f); }
    T("empty file loads nothing", chat_load(P, L, MAX_CHATS, &cur), 0);

    printf("\n  -- the array is not touched when a load fails --\n");
    seed(); chat_save(P, C, 2, 0);
    memset(L, 0, sizeof L);
    chat_load(P, L, MAX_CHATS, &cur);
    { FILE *f = fopen(P, "wb"); fprintf(f, "garbage"); fclose(f); }
    int before_ok = strcmp(L[0].title, C[0].title) == 0;
    chat_load(P, L, MAX_CHATS, &cur);
    T("a failed load leaves earlier data intact", before_ok && strcmp(L[0].title, C[0].title) == 0, 1);

    printf("\n  -- mutation pass --\n");
    seed(); chat_save(P, C, 2, 0);
    { /* a loader that ignored the trailer would accept the truncated file */
      FILE *f = fopen(P, "rb"); fseek(f, 0, SEEK_END); long sz = ftell(f);
      char *b = malloc((size_t)sz); rewind(f); fread(b, 1, (size_t)sz, f); fclose(f);
      f = fopen(P, "wb"); fwrite(b, 1, (size_t)sz - 8, f); fclose(f); free(b);
      int n = chat_load(P, L, MAX_CHATS, &cur);
      printf("  %s  mutant: without the trailer check a torn file would load (%d)\n",
             n == 0 ? "CAUGHT" : "MISSED", n);
      if (n != 0) F++; }

    remove(P);
    printf("\n  %s: session persistence, %d failure(s)\n\n", F ? "FAIL" : "PASS", F);
    return F != 0;
}
