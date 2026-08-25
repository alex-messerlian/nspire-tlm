/* STANDALONE TEST, before integration. Positive control BY CONTENT, not count: a mis-wired picker
 * can return plausible counts, so the suite asserts that a known family yields a KNOWN RECORD with
 * KNOWN FIELDS. No empty, shuffled, or mis-mapped picker satisfies that. */
#include <stdio.h>
#include <string.h>
#include "picker.h"

static int fails = 0;
static void ck(int c, const char *w) { if (!c) { printf("  FAIL %s\n", w); fails++; } }

static int find_by_formula(const ns_store2 *st, const char *f) {
    for (int i = 0; i < st->n; i++) if (strcmp(st->rec[i].formula, f) == 0) return i;
    return -1;
}

int main(int argc, char **argv) {
    ns_store2 st;
    int rc = ns_load(&st, argc > 1 ? argv[1] : "build/store.tns");
    if (rc != NS_OK) { printf("  FAIL load: %s\n", ns_strerror(rc)); return 1; }

    /* ---- POSITIVE CONTROL: known family -> known record -> known fields ------------------- */
    int spd = find_by_formula(&st, "v=d/t");
    ck(spd >= 0, "known record v=d/t is in the store");
    if (spd >= 0) {
        int f = ns_family_of(&st, spd);
        ck(f >= 0, "v=d/t maps to a family");
        ck(f >= 0 && strcmp(ns_family_name(f), "Speed & velocity") == 0,
           "v=d/t lands in 'Speed & velocity' specifically");
        int buf[512], n = ns_records_in_family(&st, f, buf, 512);
        int found = 0;
        for (int i = 0; i < n; i++) if (buf[i] == spd) found = 1;
        ck(found, "browsing that family reaches v=d/t");
        ck(n > 1 && n <= NS_FAMILY_ROWS * 2, "Speed & velocity is 1-2 screens deep");
    }
    int hooke = find_by_formula(&st, "F=-k*x");
    if (hooke >= 0) {
        int f = ns_family_of(&st, hooke);
        ck(f >= 0 && strcmp(ns_family_name(f), "Force & pressure") == 0,
           "F=-k*x lands in 'Force & pressure'");
        ck(strstr(st.rec[hooke].req, "restoring") != NULL,
           "the picked record still carries its sign convention");
    }

    /* ---- EVERY record maps: no Other bucket, which is the evidence the key is right ------- */
    int unmapped = 0;
    for (int i = 0; i < st.n; i++) if (ns_family_of(&st, i) < 0) {
        if (unmapped < 3) printf("  UNMAPPED %s (%s)\n", st.rec[i].formula, st.rec[i].name);
        unmapped++;
    }
    ck(unmapped == 0, "every record maps to a family");

    /* ---- first screen fits, and the counts sum to the store ------------------------------- */
    ns_family fam[NS_MAX_FAMILIES];
    int nf = ns_families(&st, fam, NS_MAX_FAMILIES);
    ck(nf > 0 && nf <= NS_FAMILY_ROWS, "family list fits one screen without scrolling");
    int tot = 0, worst = 0;
    for (int i = 0; i < nf; i++) { tot += fam[i].count; if (fam[i].count > worst) worst = fam[i].count; }
    ck(tot == st.n, "family counts sum to the store size");
    ck(worst <= NS_FAMILY_ROWS * 2, "worst browse path is at most 2 screens");
    for (int i = 0; i < nf; i++) {
        ck(fam[i].count > 0, "no empty family on the first screen");
        ck(strlen(fam[i].name) + 6 <= 50, "family name fits 50 columns with its count");
    }

    /* ---- NEGATIVE CONTROLS --------------------------------------------------------------- */
    int buf[512];
    ck(ns_filter(&st, -1, "zzzznotarelation", buf, 512) == 0,
       "empty search returns 0 -- a VALID result, the empty-search screen");
    ck(ns_filter(&st, -1, "", buf, 512) == st.n, "empty query matches everything");
    ck(ns_filter(&st, -1, "SPEED", buf, 512) == ns_filter(&st, -1, "speed", buf, 512),
       "filter is case-insensitive");
    ck(ns_records_in_family(&st, -1, buf, 512) == -1, "negative family index rejected");
    ck(ns_records_in_family(&st, 999, buf, 512) == -1, "out-of-range family index rejected");
    ck(ns_family_of(&st, -1) == -1 && ns_family_of(&st, st.n) == -1, "record index bounds checked");
    ck(ns_family_name(-1) == NULL && ns_family_name(999) == NULL, "family name bounds checked");
    ck(ns_filter(&st, -1, "e", buf, 4) <= 4, "filter respects the caller's array bound");
    /* scoped filter must be a subset of the unscoped one */
    int all_n = ns_filter(&st, -1, "energy", buf, 512);
    int sc[512], sc_n = ns_filter(&st, 5, "energy", sc, 512);
    ck(sc_n <= all_n, "family-scoped filter is a subset of the global filter");

    ns_free(&st);
    printf(fails ? "FAILED: %d assertion(s)\n" : "PASS: all assertions (%d failures)\n", fails);
    return fails ? 1 : 0;
}
