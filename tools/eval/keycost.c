/* KEYSTROKES FROM LAUNCH TO ANSWER, with and without the Suggested section.
 *
 * @5 is not the number this section exists to move -- what a student pays is keypresses. Both arms
 * drive the REAL pk_key state machine and ASSERT they arrive at the target record, so this counts
 * the shipped navigation rather than a model of it.
 *
 *   arm A  the picker as it shipped before Suggested: family list only, cursor preselected to the
 *          ranker's top-1 family. The SAME ranker as arm B, so the comparison isolates the section.
 *   arm B  Suggested: five ranked records above the families, cursor on the first.
 *
 * Both arms pay the same cost to type the question and press enter; it is included, because the
 * question asked was launch-to-answer and a selection-only delta would overstate the effect.
 *
 * usage: keycost <store.tns> < questions_and_gold.tsv     (question<TAB>gold_formula)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../src/store/pickui.h"

static ns_store2 ST;

static const char *nrm(const char *f, char *buf, int cap) {   /* drop parens and spaces */
    int o = 0;
    for (const char *c = f; *c && o < cap - 1; c++)
        if (*c != '(' && *c != ')' && *c != ' ') buf[o++] = *c;
    buf[o] = 0; return buf;
}
static int find_rec(const char *goldn) {
    char b[256];
    for (int i = 0; i < ST.n; i++) if (!strcmp(nrm(ST.rec[i].formula, b, sizeof b), goldn)) return i;
    return -1;
}
/* Press UP/DOWN until sel == target. Returns the count, or -1 if it never arrives. */
static int walk_to(pk_state *p, int target) {
    int keys = 0, r;
    while (p->sel != target && keys < 400) {
        pk_key(p, &ST, p->sel < target ? K_DOWN : K_UP, &r);
        keys++;
    }
    return p->sel == target ? keys : -1;
}
/* Navigate from the family list to `rec` by browsing, and press enter. -1 if it does not arrive. */
static int browse_to(pk_state *p, int rec, int base_row_of_family) {
    int keys = walk_to(p, base_row_of_family);
    if (keys < 0) return -1;
    int r = -1;
    pk_key(p, &ST, K_ENTER, &r); keys++;
    if (p->level != PK_RECORD) return -1;
    int row = -1;
    for (int i = 0; i < p->nhit; i++) if (p->hit[i] == rec) { row = i; break; }
    if (row < 0) return -1;
    int w = walk_to(p, row);
    if (w < 0) return -1;
    keys += w;
    if (pk_key(p, &ST, K_ENTER, &r) != PK_ACT_PICKED || r != rec) return -1;
    return keys + 1;
}

int main(int argc, char **argv) {
    if (ns_load(&ST, argc > 1 ? argv[1] : "build/store.tns") != NS_OK) return 2;
    if (argc > 2) pk_set_max_sug(atoi(argv[2]));
    char line[2048];
    printf("q_chars\tarmA\tarmB\tin_sug\n");
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        const char *q = line, *gold = tab + 1;
        int rec = find_rec(gold);
        if (rec < 0) continue;
        int fam = ns_family_of(&ST, rec);
        if (fam < 0) continue;

        static pk_state A, B;
        /* arm A: no shortlist, cursor preselected to the ranker's top-1 family -- what shipped. */
        pk_open(&A, &ST, "");
        {   static ns_ask a; ask_parse(q, &a);
            int top[1];
            if (ask_rank(&ST, q, &a.in, ASK_QTY, top, 1)) {
                int f = ns_family_of(&ST, top[0]);
                if (f >= 0) { A.sel = f; }
            }
        }
        int ka = browse_to(&A, rec, fam);

        /* arm B: Suggested. */
        pk_open(&B, &ST, q);
        int kb = -1, in_sug = 0;
        for (int i = 0; i < B.nsug; i++) if (B.sug[i] == rec) { in_sug = 1;
            int w = walk_to(&B, i);
            int r = -1;
            if (w >= 0 && pk_key(&B, &ST, K_ENTER, &r) == PK_ACT_PICKED && r == rec) kb = w + 1;
            break; }
        if (kb < 0) { pk_open(&B, &ST, q); kb = browse_to(&B, rec, B.nsug + fam); }
        if (ka < 0 || kb < 0) continue;

        int typing = (int)strlen(q) + 1;          /* the question, then enter */
        printf("%d\t%d\t%d\t%d\n", typing, typing + ka, typing + kb, in_sug);
    }
    return 0;
}
