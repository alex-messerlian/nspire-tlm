/* The picker state machine, against the SHIPPED store.
 *
 * PICKER_SPEC's test plan, implemented as written: "Positive control by content, not count --
 * assert that picking a known family yields a known record with known fields, which no empty or
 * mis-wired picker can satisfy. Negative controls: empty search, filter to zero, esc from every
 * level, a family with one record, and the longest name in the store."
 *
 * test_picker survived its own negative control by never calling ns_filter. That is recorded in
 * the project log as one of six survivals, so every assertion here names the call it exercises.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/pickui.h"

static int fails = 0, ran = 0;
static ns_store2 ST;
static pk_state P;

static void ck(int ok, const char *what, const char *got) {
    ran++;
    if (!ok) { fails++; printf("  FAIL %-52s got: %s\n", what, got ? got : ""); }
}
static void keys(const char *s) { int r; for (const char *c = s; *c; c++) pk_key(&P, &ST, *c, &r); }
static pk_action key1(int k, int *r) { return pk_key(&P, &ST, k, r); }

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "build/store.tns";
    int rc = ns_load(&ST, path);
    if (rc != NS_OK) { printf("load %s: %s\n", path, ns_strerror(rc)); return 2; }
    char m[512];
    int r;

    printf("store %s: %d records\n\n", path, ST.n);

    /* ---- every record is reachable by browsing ------------------------------------------- */
    printf("-- coverage ------------------------------------------------------------\n");
    pk_open(&P, &ST, "");
    int total = 0, worst = 0, empty_fams = 0;
    for (int f = 0; f < P.nfam; f++) {
        total += P.fams[f].count;
        if (P.fams[f].count > worst) worst = P.fams[f].count;
        if (P.fams[f].count == 0) empty_fams++;
    }
    snprintf(m, sizeof m, "mapped=%d/%d fams=%d worst=%d empty=%d", total, ST.n, P.nfam, worst, empty_fams);
    ck(total == ST.n, "every record reachable from some family", m);
    ck(P.nfam <= PK_ROWS, "family list fits one screen, no scrolling", m);
    ck(empty_fams == 0, "no empty family (an unopenable row)", m);
    /* THE SPEC'S NUMBER IS 27 RECORDS, and that is what is asserted. Its "2 screens" gloss was
     * computed at 15 rows a screen; 15 did not fit once the question stayed on screen, so at the
     * measured 13 the same 27 records span 3 screens. Screens are a proxy for effort anyway --
     * what a student pays is arrow presses or one or two filter characters, and neither changed.
     * Asserting the proxy instead of the quantity would have failed here for no reason a reader
     * would care about. */
    snprintf(m, sizeof m, "worst=%d records -> %d screens of %d",
             worst, (worst + PK_ROWS - 1) / PK_ROWS, PK_ROWS);
    ck(worst <= 27, "worst browse path is 27 records", m);

    /* A FAMILY LISTS ONLY ITS OWN RECORDS. Without this, dropping the scope test in ns_filter
     * left every assertion in this file passing while each family showed the whole store. */
    for (int fam = 0; fam < P.nfam; fam++) {
        pk_open(&P, &ST, "");
        P.sel = fam; key1(K_ENTER, &r);
        int wrong = 0;
        for (int i = 0; i < P.nhit; i++) if (ns_family_of(&ST, P.hit[i]) != fam) wrong++;
        snprintf(m, sizeof m, "%s: nhit=%d want=%d off-family=%d",
                 ns_family_name(fam), P.nhit, P.fams[fam].count, wrong);
        ck(P.nhit == P.fams[fam].count && wrong == 0, "family lists exactly its own records", m);
    }

    /* ---- POSITIVE CONTROL BY CONTENT ------------------------------------------------------
     * Open the family a known record lives in, filter to it, pick it, and assert the RECORD --
     * formula and lhs, not a count. An empty or mis-wired picker cannot satisfy this. */
    printf("\n-- positive control, by content ---------------------------------------\n");
    pk_open(&P, &ST, "");
    key1(K_ESC, &r);                                       /* (checked separately below) */
    pk_open(&P, &ST, "");
    keys("hooke");                                          /* a letter opens search-all */
    snprintf(m, sizeof m, "level=%d nhit=%d q=%s", P.level, P.nhit, P.q);
    ck(P.level == PK_RECORD && P.nhit >= 1, "typing at the family list searches all records", m);
    pk_action a = key1(K_ENTER, &r);
    const char *f = (r >= 0) ? ST.rec[r].formula : "";
    const char *l = (r >= 0) ? ST.rec[r].lhs : "";
    snprintf(m, sizeof m, "action=%d rec=%d formula=%s lhs=%s", a, r, f, l);
    ck(a == PK_ACT_PICKED && r >= 0 && !strcmp(f, "F=-k*x") && !strcmp(l, "F"),
       "\"hooke\" -> enter yields F=-k*x with lhs F", m);

    /* ---- esc goes up EXACTLY one level, from every level ---------------------------------- */
    printf("\n-- esc goes up exactly one level ---------------------------------------\n");
    pk_open(&P, &ST, "");
    key1(K_ENTER, &r);                                      /* family 0 -> record list */
    snprintf(m, sizeof m, "level=%d fam=%d", P.level, P.fam);
    ck(P.level == PK_RECORD, "enter on a family opens its record list", m);
    key1(K_ESC, &r);
    snprintf(m, sizeof m, "level=%d sel=%d", P.level, P.sel);
    ck(P.level == PK_FAMILY, "esc from record list returns to the family list", m);
    a = key1(K_ESC, &r);
    snprintf(m, sizeof m, "action=%d", a);
    ck(a == PK_ACT_ASK_ANYWAY, "esc at the family list is ASK ANYWAY (Form C)", m);
    /* and it must NOT skip a level: esc deep in a filtered list lands on families, not Form C */
    pk_open(&P, &ST, "");
    keys("hooke");
    a = key1(K_ESC, &r);
    snprintf(m, sizeof m, "action=%d level=%d", a, P.level);
    ck(a == PK_ACT_NONE && P.level == PK_FAMILY, "esc from a FILTERED list does not skip to Form C", m);
    /* esc back from a family returns the cursor TO that family, not to row 0 */
    pk_open(&P, &ST, "");
    key1(K_DOWN, &r); key1(K_DOWN, &r); key1(K_ENTER, &r); key1(K_ESC, &r);
    snprintf(m, sizeof m, "sel=%d", P.sel);
    ck(P.sel == 2, "esc restores the cursor to the family you came from", m);

    /* ---- empty search is a screen, not an error ------------------------------------------- */
    printf("\n-- empty search --------------------------------------------------------\n");
    pk_open(&P, &ST, "");
    keys("zzzzqqq");
    snprintf(m, sizeof m, "nhit=%d level=%d empty=%d", P.nhit, P.level, pk_is_empty_search(&P));
    ck(P.nhit == 0 && pk_is_empty_search(&P), "a filter matching nothing is the empty screen", m);
    a = key1('a', &r);
    snprintf(m, sizeof m, "action=%d", a);
    ck(a == PK_ACT_ASK_ANYWAY, "'a' on the empty screen is ASK ANYWAY", m);
    /* 'a' must NOT hijack typing when there ARE hits -- and the case has to be typed at the
     * RECORD level, which is where that branch lives. Typing it at the family list goes through
     * enter_records() instead, so a control on the record-level handler survived. */
    pk_open(&P, &ST, "");
    keys("m");                                   /* now at the record level with hits */
    int before_a = P.nhit;
    a = key1('a', &r);
    snprintf(m, sizeof m, "action=%d q=%s nhit=%d (was %d)", a, P.q, P.nhit, before_a);
    ck(a == PK_ACT_NONE && !strcmp(P.q, "ma"), "'a' types normally at the record level", m);

    /* ---- backspace is reversible ----------------------------------------------------------- */
    printf("\n-- type-to-filter is reversible ----------------------------------------\n");
    pk_open(&P, &ST, "");
    keys("h");
    int after_h = P.nhit;
    keys("ookezzz");
    ck(P.nhit == 0, "filtered to zero", "");
    for (int i = 0; i < 7; i++) key1(K_BACK, &r);
    snprintf(m, sizeof m, "q=%s nhit=%d (was %d)", P.q, P.nhit, after_h);
    ck(P.nhit == after_h && !strcmp(P.q, "h"), "backspace restores the previous candidate set", m);
    /* backspace at an empty query must NOT navigate -- esc is the way up */
    pk_open(&P, &ST, "");
    key1(K_ENTER, &r);
    key1(K_BACK, &r);
    snprintf(m, sizeof m, "level=%d", P.level);
    ck(P.level == PK_RECORD, "backspace on an empty query does not go up a level", m);

    /* ---- selection and scrolling stay in range -------------------------------------------- */
    printf("\n-- bounds --------------------------------------------------------------\n");
    pk_open(&P, &ST, "");
    for (int i = 0; i < 200; i++) key1(K_UP, &r);
    snprintf(m, sizeof m, "sel=%d scroll=%d", P.sel, P.scroll);
    ck(P.sel == 0 && P.scroll == 0, "up past the top clamps", m);
    for (int i = 0; i < 500; i++) key1(K_DOWN, &r);
    snprintf(m, sizeof m, "sel=%d nfam=%d", P.sel, P.nfam);
    ck(P.sel == P.nfam - 1, "down past the end clamps to the last family", m);
    /* the largest family: scroll must reach its last record and no further */
    pk_open(&P, &ST, "");
    int big = 0; for (int i = 1; i < P.nfam; i++) if (P.fams[i].count > P.fams[big].count) big = i;
    P.sel = big; key1(K_ENTER, &r);
    for (int i = 0; i < 500; i++) key1(K_DOWN, &r);
    snprintf(m, sizeof m, "fam=%d nhit=%d sel=%d scroll=%d", big, P.nhit, P.sel, P.scroll);
    ck(P.sel == P.nhit - 1 && P.scroll == P.nhit - PK_ROWS, "largest family scrolls to its last row", m);
    a = key1(K_ENTER, &r);
    ck(a == PK_ACT_PICKED && r >= 0, "the last row of the largest family is pickable", m);

    /* ---- enter on nothing does nothing ----------------------------------------------------- */
    pk_open(&P, &ST, "");
    keys("zzzzqqq");
    a = key1(K_ENTER, &r);
    snprintf(m, sizeof m, "action=%d rec=%d", a, r);
    ck(a == PK_ACT_NONE && r == -1, "enter with zero hits picks nothing", m);

    /* ---- SUGGESTIONS ---------------------------------------------------------------------
     * Additive: rows [0,nsug) sit above the families and every family keeps its order. Each case
     * below states what must NOT change as well as what must. */
    printf("\n-- suggestions ---------------------------------------------------------\n");
    const char *Q = "A spring with k=250 N/m is stretched 0.08 m. Find the force.";
    pk_open(&P, &ST, Q);
    snprintf(m, sizeof m, "nsug=%d rows=%d sel=%d", P.nsug, P.rows, P.sel);
    ck(P.nsug > 0 && P.nsug <= PK_MAX_SUG, "a question produces a shortlist, capped at 5", m);
    ck(P.rows == PK_ROWS - 2, "two section headers are reserved out of the page", m);
    ck(P.sel == 0, "the cursor starts on the top suggestion", m);
    ck(pk_row_is_sug(&P, 0) && !pk_row_is_sug(&P, P.nsug), "rows [0,nsug) are the suggestions", m);

    /* ADDITIVE: the families keep their identity and order, just offset. */
    {   int shifted = 0;
        for (int f = 0; f < P.nfam; f++) if (pk_row_family(&P, P.nsug + f) != f) shifted++;
        snprintf(m, sizeof m, "misplaced=%d nfam=%d nsug=%d", shifted, P.nfam, P.nsug);
        ck(shifted == 0, "every family sits at nsug+f, in its original order", m);
    }
    {   pk_state B; pk_open(&B, &ST, "");
        int same = (B.nsug == 0 && B.rows == PK_ROWS && B.nfam == P.nfam);
        for (int f = 0; f < B.nfam; f++) if (B.fams[f].count != P.fams[f].count) same = 0;
        snprintf(m, sizeof m, "nsug=%d rows=%d", B.nsug, B.rows);
        ck(same, "with no suggestions the model is exactly what it was", m);
    }

    /* ENTER ON A SUGGESTION PICKS IT -- one keypress, which is the point of the section. */
    pk_open(&P, &ST, Q);
    a = key1(K_ENTER, &r);
    snprintf(m, sizeof m, "action=%d rec=%d formula=%s", a, r, r >= 0 ? ST.rec[r].formula : "");
    ck(a == PK_ACT_PICKED && r >= 0 && r == P.sug[0], "enter on a suggestion picks that record", m);

    /* ENTER ON A FAMILY STILL OPENS IT -- browsing underneath is unchanged. */
    pk_open(&P, &ST, Q);
    P.sel = P.nsug + 4;
    a = key1(K_ENTER, &r);
    snprintf(m, sizeof m, "action=%d level=%d fam=%d want=%d", a, P.level, P.fam, 4);
    ck(a == PK_ACT_NONE && P.level == PK_RECORD && P.fam == 4, "enter on a family opens it", m);
    key1(K_ESC, &r);
    snprintf(m, sizeof m, "sel=%d want=%d", P.sel, P.nsug + 4);
    ck(P.sel == P.nsug + 4, "esc returns to that family's row, offset past the suggestions", m);

    /* ESC IS UNCHANGED BY THE RULING: ask anyway at the family level, wherever the cursor is. */
    pk_open(&P, &ST, Q);
    a = key1(K_ESC, &r);
    snprintf(m, sizeof m, "action=%d sel=0 (a suggestion)", a);
    ck(a == PK_ACT_ASK_ANYWAY, "esc on a suggestion row is still ASK ANYWAY", m);

    /* arrows cross the boundary in both directions and nothing is skipped */
    pk_open(&P, &ST, Q);
    for (int i = 0; i < P.nsug; i++) key1(K_DOWN, &r);
    snprintf(m, sizeof m, "sel=%d nsug=%d fam=%d", P.sel, P.nsug, pk_row_family(&P, P.sel));
    ck(P.sel == P.nsug && pk_row_family(&P, P.sel) == 0, "down past the suggestions reaches family 0", m);
    key1(K_UP, &r);
    ck(pk_row_is_sug(&P, P.sel), "and up returns into the suggestions", m);
    for (int i = 0; i < 500; i++) key1(K_DOWN, &r);
    snprintf(m, sizeof m, "sel=%d last=%d", P.sel, P.nsug + P.nfam - 1);
    ck(P.sel == P.nsug + P.nfam - 1, "down clamps to the last family, not the last suggestion", m);

    /* TAB leaves the shortlist in ONE key, landing in the browse section. Measured reason: a
     * miss cost 19 selection keys against 15 with no shortlist, and the whole difference was the
     * walk past five rows. With TAB the miss is 15 -- the worst case is now exactly 1 key. */
    pk_open(&P, &ST, Q);
    a = key1(K_TAB, &r);
    snprintf(m, sizeof m, "action=%d sel=%d nsug=%d fam=%d", a, P.sel, P.nsug, pk_row_family(&P, P.sel));
    ck(a == PK_ACT_NONE && !pk_row_is_sug(&P, P.sel) && pk_row_family(&P, P.sel) >= 0,
       "tab from a suggestion lands in the browse section", m);
    /* AGAINST THE INDEPENDENTLY COMPUTED FAMILY, not against P.browse_row. Comparing sel to
     * browse_row is self-consistent: a mutation that sets browse_row wrong moves both sides and
     * the assertion still passes -- which is exactly how test_bubble survived its own control by
     * calling qbubble_textw() on both sides of a comparison. */
    {   int want = ns_family_of(&ST, P.sug[0]);
        snprintf(m, sizeof m, "landed fam=%d want=%d (from sug[0])", pk_row_family(&P, P.sel), want);
        ck(want > 0, "the case is LIVE: the predicted family is not 0, so a wrong landing shows", m);
        ck(pk_row_family(&P, P.sel) == want, "and on the family the ranker predicts, not family 0", m);
    }
    a = key1(K_TAB, &r);
    snprintf(m, sizeof m, "sel=%d", P.sel);
    ck(pk_row_is_sug(&P, P.sel) && P.sel == 0, "tab again returns to the shortlist", m);
    /* with no shortlist TAB is inert rather than doing something arbitrary */
    pk_open(&P, &ST, "");
    key1(K_TAB, &r);
    snprintf(m, sizeof m, "sel=%d nsug=%d", P.sel, P.nsug);
    ck(P.sel == 0, "tab is inert when there is no shortlist", m);

    /* typing still leaves for search-all, from a suggestion row */
    pk_open(&P, &ST, Q);
    keys("hooke");
    snprintf(m, sizeof m, "level=%d nhit=%d", P.level, P.nhit);
    ck(P.level == PK_RECORD && P.nhit >= 1, "typing on a suggestion row still searches all", m);

    /* ---- the longest name in the store ------------------------------------------------------ */
    int longest = 0; size_t lw = 0;
    for (int i = 0; i < ST.n; i++) { size_t w = strlen(ST.rec[i].name ? ST.rec[i].name : "");
                                     if (w > lw) { lw = w; longest = i; } }
    pk_open(&P, &ST, "");
    P.sel = ns_family_of(&ST, longest); key1(K_ENTER, &r);
    int found = 0; for (int i = 0; i < P.nhit; i++) if (P.hit[i] == longest) found = 1;
    snprintf(m, sizeof m, "len=%d name=%.60s", (int)lw, ST.rec[longest].name);
    ck(found, "the longest name is reachable in its family", m);

    printf("\n%s  %d/%d\n", fails ? "FAIL" : "PASS", ran - fails, ran);
    return fails ? 1 : 0;
}
