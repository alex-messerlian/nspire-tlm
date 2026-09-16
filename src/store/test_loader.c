/* STANDALONE TEST BINARY, run before any integration and before any device round-trip.
 *
 * The load path's dangerous failure is not a crash -- it is a SILENTLY EMPTY LOAD reading as
 * success. So the suite is built around a POSITIVE CONTROL with known content: it asserts a
 * specific record is present with specific fields, which no empty or partial load can satisfy.
 * Counting records is not enough; a truncated file has a plausible count.
 *
 * Exit status is the result. Output is for humans and is never parsed by a caller. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "loader.h"

static int fails = 0;
static void ck(int cond, const char *what) {
    if (!cond) { printf("  FAIL %s\n", what); fails++; }
}
static void ck_eq(int got, int want, const char *what) {
    if (got != want) { printf("  FAIL %s: got %d (%s), want %d (%s)\n",
                              what, got, ns_strerror(got), want, ns_strerror(want)); fails++; }
}
static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb"); if (!f) { printf("  FAIL cannot write %s\n", path); fails++; return; }
    fputs(content, f); fclose(f);
}

int main(int argc, char **argv) {
    const char *real = argc > 1 ? argv[1] : "build/store.tns";
    ns_store2 st;

    /* ---- POSITIVE CONTROL: known-good file, known content -------------------------------- */
    int rc = ns_load(&st, real);
    ck_eq(rc, NS_OK, "load real store");
    if (rc == NS_OK) {
        ck(st.n > 100, "record count is plausible (>100)");
        /* Content assertions -- an empty or truncated load CANNOT satisfy these. */
        const ns_rec2 *hooke = NULL, *speed = NULL;
        for (int i = 0; i < st.n; i++) {
            if (strcmp(st.rec[i].formula, "F=-k*x") == 0) hooke = &st.rec[i];
            if (strcmp(st.rec[i].formula, "v=d/t")  == 0) speed = &st.rec[i];
        }
        ck(hooke != NULL, "known record F=-k*x is present");
        if (hooke) {
            ck(strcmp(hooke->lhs, "F") == 0,            "F=-k*x has lhs F");
            /* 3, not 2: the packer emits the LHS variable first so the device can label the
             * answer and key the picker family. Changed deliberately; this assertion is the
             * regression guard that caught the format change. */
            ck(hooke->nvars == 3,                        "F=-k*x has 3 variables (F, k, x)");
            ck(strcmp(hooke->var[0], "F") == 0,          "the LHS variable is packed FIRST");
            ck(strcmp(hooke->unit[0], "N") == 0,         "the LHS carries its unit");
            ck(strcmp(hooke->name, "Hooke's law") == 0,  "F=-k*x carries its name");
            ck(strstr(hooke->req, "restoring") != NULL,  "F=-k*x carries the sign convention in req");
        }
        ck(speed != NULL, "known record v=d/t is present");
        if (speed) ck(strcmp(speed->lhs, "v") == 0, "v=d/t has lhs v");
        /* Every record structurally complete -- no half-parsed tail.
         *
         * TWO KINDS NOW, and the difference is DECLARED rather than the '=' test being relaxed.
         * A compute record's formula is a relation and must contain '='. A KNOWLEDGE record
         * (K-TEXT) has unit[0] == "text" and its formula is a bare term, so "aberration" has no
         * '=' and is correct. Weakening the test to "formula is non-empty" would have let a
         * truncated compute record through, which is the whole reason this check exists.
         *
         * The knowledge branch is checked HARDER than the old test, not softer: exactly one
         * variable, that variable IS the lhs (which is what makes assemble.c emit missing:none
         * without a new branch), and a non-empty req, which is where the meaning lives. */
        int bad = 0, nk = 0, nc = 0;
        for (int i = 0; i < st.n; i++) {
            const ns_rec2 *r = &st.rec[i];
            if (!r->rid || !r->lhs || !r->formula || !r->name || !r->req) { bad++; continue; }
            int knowledge = (r->nvars == 1 && r->unit[0] && strcmp(r->unit[0], "text") == 0);
            if (knowledge) {
                nk++;
                if (strcmp(r->var[0], r->lhs) != 0) bad++;   /* else assemble.c reports it missing */
                if (!r->req[0]) bad++;                        /* the meaning IS field 3 */
                if (strchr(r->formula, '=') != NULL) bad++;   /* a term, not a relation */
            } else {
                nc++;
                if (strchr(r->formula, '=') == NULL) bad++;
            }
            for (int k = 0; k < r->nvars; k++) if (!r->var[k] || !r->unit[k]) bad++;
        }
        ck(bad == 0, "every record structurally complete");
        printf("  (%d compute records, %d knowledge records)\n", nc, nk);
        /* If the knowledge tier is packed at all, it must be packed correctly end to end. */
        if (nk) {
            const ns_rec2 *kd = NULL;
            for (int i = 0; i < st.n; i++)
                if (st.rec[i].nvars == 1 && !strcmp(st.rec[i].unit[0], "text")
                    && !strcmp(st.rec[i].formula, "aberration")) { kd = &st.rec[i]; break; }
            ck(kd != NULL, "a known knowledge record is present");
            if (kd) {
                ck(strcmp(kd->name, "aberration") == 0, "the term is the record NAME, so it is retrievable");
                ck(strstr(kd->req, "converge") != NULL, "the meaning is in req, which assemble emits as field 3");
                ck(kd->cval[0] == NULL || kd->cval[0][0] == '\0',
                   "cval is empty, so no \" Given \" is emitted for a definition");
            }
        }
        ns_free(&st);
        ck(st.rec == NULL && st.slab == NULL && st.n == 0, "ns_free clears the struct");
    }

    /* ---- NEGATIVE CONTROLS: every failure mode gets a DISTINCT code ----------------------- */
    ck_eq(ns_load(&st, "/nonexistent/store.tns"), NS_ERR_OPEN,  "missing file");
    ck_eq(ns_load(NULL, real),                    NS_ERR_ARG,   "null store");
    ck_eq(ns_load(&st, NULL),                     NS_ERR_ARG,   "null path");

    write_file("/tmp/t_empty.tns", "");
    ck_eq(ns_load(&st, "/tmp/t_empty.tns"), NS_ERR_EMPTY_FILE, "empty file");

    write_file("/tmp/t_magic.tns", "WRONGMAGIC\n3\n");
    ck_eq(ns_load(&st, "/tmp/t_magic.tns"), NS_ERR_MAGIC, "bad magic");

    write_file("/tmp/t_count.tns", "NSTORE1\n0\nEND\t0\n");
    ck_eq(ns_load(&st, "/tmp/t_count.tns"), NS_ERR_COUNT, "zero count rejected, not 'empty success'");

    /* THE ONE THAT MATTERS: header says 2, only 1 record present, END agrees with header. */
    write_file("/tmp/t_trunc.tns",
        "NSTORE1\n2\nR\tabc\tv\tv=d/t\tspeed\tcond\t2\nV\td\tm\t\nV\tt\ts\t\nEND\t2\n");
    ck_eq(ns_load(&st, "/tmp/t_trunc.tns"), NS_ERR_TRUNC, "truncated store must NOT read as success");

    write_file("/tmp/t_nend.tns", "NSTORE1\n1\nR\tabc\tv\tv=d/t\tspeed\tcond\t0\n");
    ck_eq(ns_load(&st, "/tmp/t_nend.tns"), NS_ERR_TRUNC, "missing END marker");

    write_file("/tmp/t_parse.tns", "NSTORE1\n1\nX\tjunk\n");
    ck_eq(ns_load(&st, "/tmp/t_parse.tns"), NS_ERR_PARSE, "unknown record tag");

    write_file("/tmp/t_shortv.tns", "NSTORE1\n1\nR\tabc\tv\tv=d/t\tspeed\tcond\t2\nV\td\tm\t\nEND\t1\n");
    ck(ns_load(&st, "/tmp/t_shortv.tns") != NS_OK, "record claiming 2 vars with 1 present must fail");

    printf(fails ? "FAILED: %d assertion(s)\n" : "PASS: all assertions (%d failures)\n", fails);
    return fails ? 1 : 0;
}
