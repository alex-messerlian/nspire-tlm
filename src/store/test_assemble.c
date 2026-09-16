/* STANDALONE TEST before integration. Positive control by CONTENT: the assembled prompt for a
 * known record with known inputs must equal a known string, byte for byte. A count or a length
 * check would pass a prompt with the wrong record in it. */
#include <stdio.h>
#include <string.h>
#include "assemble.h"
#include "picker.h"

static int fails = 0;
static void ck(int c, const char *w) { if (!c) { printf("  FAIL %s\n", w); fails++; } }
static void ck_str(const char *got, const char *want, const char *w) {
    if (strcmp(got, want)) { printf("  FAIL %s\n    got  %s\n    want %s\n", w, got, want); fails++; }
}
static int find_f(const ns_store2 *st, const char *f) {
    for (int i = 0; i < st->n; i++) if (!strcmp(st->rec[i].formula, f)) return i;
    return -1;
}

int main(int argc, char **argv) {
    ns_store2 st;
    if (ns_load(&st, argc > 1 ? argv[1] : "build/store.tns") != NS_OK) { printf("  FAIL load\n"); return 1; }
    char buf[NS_PROMPT_MAX];

    int spd = find_f(&st, "v=d/t");
    ck(spd >= 0, "v=d/t present");
    if (spd >= 0) {
        /* ---- POSITIVE CONTROL: exact expected prompt ------------------------------------- */
        ns_input in = {{"d","t"},{"150","12"},2};
        int n = ns_assemble(buf, sizeof buf, &st.rec[spd], "A car goes 150 m in 12 s. Find the speed.", &in);
        ck(n > 0, "assemble returns a length");
        /* THE LEAD-IN IS PART OF THE CONTRACT. This assertion previously expected the givens
         * appended bare -- "Find the speed. d = 150, t = 12." -- and that form occurs in 0.04% of
         * the 232,758 training questions carrying a value, all of them raw OpenStax artifacts.
         * 99.96% use one of 16 lead-in frames. The device therefore asked every question it has
         * ever asked in a surface the model had effectively never seen. */
        ck_str(buf, "<q>A car goes 150 m in 12 s. Find the speed. Given d = 150, t = 12.</q><r>"
                    "v=d/t | v:m/s d:m t:s | missing:none | standard conditions | fit:high",
               "exact prompt for v=d/t, with the entered values IN the question");
        ck(strstr(buf, "d = 150") != NULL,
           "entered values reach the prompt -- without this the model invents its inputs");
        /* 'standard conditions' is the DEFAULT: this record carries no req of its own. The test
         * first asserted 'constant speed' from the old hand-written fixture -- the fixture, not
         * the store. Assertions must be written against the shipped data. */
        /* ---- missing: is the only computation; drop t --------------------------------- */
        ns_input in2 = {{"d"},{"150"},1};
        ns_assemble(buf, sizeof buf, &st.rec[spd], "A car goes 150 m. Find the speed.", &in2);
        ck(strstr(buf, "missing:t") != NULL, "omitting t yields missing:t");
        ck(strstr(buf, "fit:high") != NULL, "fit is high by construction even when a value is missing");
        /* the regression guard for the bug the end-to-end run found */
        ck(buf[strlen(buf)-1] != '>' || strstr(buf, "fit:high<a>") == NULL,
           "the ANSWER prompt must NOT pre-open <a> -- that makes a tool call impossible");
        /* ---- the LHS is never a required INPUT ---------------------------------------- */
        ns_input in3 = {{"d","t"},{"150","12"},2};
        ns_assemble(buf, sizeof buf, &st.rec[spd], "q", &in3);
        ck(strstr(buf, "missing:v") == NULL, "the solved-for variable is NOT reported missing");
        ck(strstr(buf, "v:m/s") != NULL, "the LHS unit IS present, so the answer can be labelled");
    }
    /* ---- constants are inlined, not asked of the student ------------------------------ */
    int cap = find_f(&st, "C=epsilon_0*((A)/(d))");
    if (cap >= 0) {
        ns_input in = {{"A","d"},{"2","3"},2};
        ns_assemble(buf, sizeof buf, &st.rec[cap], "q", &in);
        ck(strstr(buf, "missing:epsilon_0") == NULL,
           "a record-supplied constant is never reported missing");
    }
    /* ---- FORM C ------------------------------------------------------------------------ */
    int n = ns_assemble_none(buf, sizeof buf, "What is the entropy change of steam?");
    ck(n > 0, "form C returns a length");
    ck_str(buf, "<q>What is the entropy change of steam?</q><r>none | missing:none "
                "| no matching relation | fit:low<a>", "form C exact string");
    ck(strstr(buf, "fit:low") != NULL, "form C carries fit:low -- the mechanism measured at 100% refusal");

    /* ---- A65: THE KEYPAD IS LOWERCASE AND THE STORE IS NOT ------------------------------
     *
     * device_app.c:314 upcases only while shift is held, so `f=12 d=2.5` -- verbatim the device
     * test's input -- is what a student actually sends. Before this, strcmp lost the given and the
     * prompt contradicted itself in one line: "Given f = 12" beside "missing:F". */
    {
        int w = -1;
        for (int i = 0; i < st.n; i++) if (!strcmp(st.rec[i].formula, "W=F*d")) { w = i; break; }
        ck(w >= 0, "W=F*d present");
        if (w >= 0) {
            ns_input lo = {0}, up = {0};
            lo.nvals = 2; lo.var[0] = "f"; lo.val[0] = "12"; lo.var[1] = "d"; lo.val[1] = "2.5";
            up.nvals = 2; up.var[0] = "F"; up.val[0] = "12"; up.var[1] = "d"; up.val[1] = "2.5";
            char a[NS_PROMPT_MAX], b[NS_PROMPT_MAX];
            ns_assemble(a, sizeof a, &st.rec[w], "find work", &lo);
            ns_assemble(b, sizeof b, &st.rec[w], "find work", &up);
            ck_str(a, b, "a lowercase given assembles byte-identically to the uppercase one");
            ck(strstr(a, "missing:none") != NULL, "lowercase f is NOT reported missing");
            ck(strstr(a, "Given F = 12") != NULL, "the prompt echoes the RECORD's spelling");
            ck(strstr(a, "Given f = 12") == NULL, "...and not the student's, when they differ");
        }
    }
    /* THE AMBIGUITY GUARD, exercised on a SYNTHETIC record because no record in the shipped store
     * has case-colliding variables -- measured, 0 of 164. A guard nobody has watched fire is a
     * claim, not a check, so it is fired here deliberately.
     *
     * Case is not noise in physics: T is period or temperature and t is time. On a record carrying
     * both, a typed `t` must resolve EXACTLY as it did before, never guess. */
    {
        ns_rec2 amb = {0};
        amb.formula = "x=T*t"; amb.name = "ambiguous"; amb.lhs = "x"; amb.req = ""; amb.rid = "amb1";
        amb.nvars = 3;
        amb.var[0] = "x"; amb.unit[0] = "m";  amb.cval[0] = "";
        amb.var[1] = "T"; amb.unit[1] = "s";  amb.cval[1] = "";
        amb.var[2] = "t"; amb.unit[2] = "s";  amb.cval[2] = "";
        ns_input one = {0};
        one.nvals = 1; one.var[0] = "t"; one.val[0] = "5";
        char c[NS_PROMPT_MAX];
        ck(ns_assemble(c, sizeof c, &amb, "find x", &one) > 0, "ambiguous record assembles");
        ck(strstr(c, "Given t = 5") != NULL, "an exact match still wins on an ambiguous record");
        ck(strstr(c, "missing:T") != NULL,
           "and T stays MISSING: a case-insensitive fallback must not resolve it");
        /* the other direction: two typed tokens differing only by case must not both claim one var */
        ns_rec2 one_var = {0};
        one_var.formula = "y=F"; one_var.name = "single"; one_var.lhs = "y"; one_var.req = ""; one_var.rid = "one1";
        one_var.nvars = 2;
        one_var.var[0] = "y"; one_var.unit[0] = "N"; one_var.cval[0] = "";
        one_var.var[1] = "F"; one_var.unit[1] = "N"; one_var.cval[1] = "";
        ns_input two = {0};
        two.nvals = 2; two.var[0] = "f"; two.val[0] = "1"; two.var[1] = "F"; two.val[1] = "2";
        char e[NS_PROMPT_MAX];
        ck(ns_assemble(e, sizeof e, &one_var, "find y", &two) > 0, "two-token record assembles");
        ck(strstr(e, "missing:none") != NULL,
           "the EXACT token still binds F when a case variant is also present");
    }

    /* ---- NEGATIVE CONTROLS ------------------------------------------------------------- */
    ck(ns_assemble(NULL, 10, &st.rec[0], "q", NULL) == -1, "null out rejected");
    ck(ns_assemble(buf, sizeof buf, NULL, "q", NULL) == -1, "null record rejected");
    ck(ns_assemble(buf, sizeof buf, &st.rec[0], NULL, NULL) == -1, "null question rejected");
    char tiny[16];
    ck(ns_assemble(tiny, sizeof tiny, &st.rec[0], "a long question here", NULL) == -1,
       "OVERFLOW RETURNS -1, never a truncated prompt");
    ck(ns_assemble_none(tiny, sizeof tiny, "a long question here") == -1, "form C overflow rejected");
    /* every record must assemble without overflow at the real cap */
    int over = 0;
    for (int i = 0; i < st.n; i++)
        if (ns_assemble(buf, sizeof buf, &st.rec[i], "a typical question of ordinary length", NULL) < 0) over++;
    ck(over == 0, "every record in the store assembles within NS_PROMPT_MAX");

    ns_free(&st);
    printf(fails ? "FAILED: %d assertion(s)\n" : "PASS: all assertions (%d failures)\n", fails);
    return fails ? 1 : 0;
}
