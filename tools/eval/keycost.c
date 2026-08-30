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

/* THE FILTER PATH, at its CEILING.
 *
 * Typing at the family list drops into search-all and appends, so a query of L characters costs L
 * keys, then arrows to the row, then enter. An optimal user types the shortest query that gets
 * there -- so this searches every prefix of every word of the TARGET RECORD'S OWN NAME and takes
 * the cheapest that arrives.
 *
 * THAT IS AN ORACLE, AND IT IS THE POINT: it assumes the student can name a word of the record.
 * For "Hooke's law" a student asking about a spring cannot, which is the 31.8% no-shared-word
 * finding in RESULT_RETRIEVAL_CEILING.md. So this is a CEILING on the filter path, not a
 * prediction of it -- the real cost lies between this and the browse figure. Reporting it is still
 * the right move, because if even the ceiling does not close the gap the question is settled. */
static int word_in_q(const char *q, const char *w) {          /* whole word, case-insensitive */
    for (const char *h = q; *h; h++) {
        if (h != q) { char p = h[-1]; if ((p>='a'&&p<='z')||(p>='A'&&p<='Z')) continue; }
        int k = 0;
        while (w[k] && h[k] && ((h[k]>='A'&&h[k]<='Z' ? h[k]-'A'+'a' : h[k]) == w[k])) k++;
        if (w[k]) continue;
        char nc = h[k];
        if ((nc>='a'&&nc<='z')||(nc>='A'&&nc<='Z')) continue;
        return 1;
    }
    return 0;
}

/* require_in_q: only consider words the student's OWN QUESTION contains. That is the plausible
 * filter -- a word they have already written is a word they can think of typing. Without it the
 * arm is an oracle over the record's name, which no student has. */
static int filter_cost(const char *q, int rec, int require_in_q) {
    const char *nm = ST.rec[rec].name;
    if (!nm || !nm[0]) return -1;
    int best = -1;
    char word[48];
    int w = 0;
    for (const char *c = nm; ; c++) {
        int isal = *c && ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z'));
        if (isal && w < (int)sizeof word - 1) {
            word[w++] = (*c >= 'A' && *c <= 'Z') ? (char)(*c - 'A' + 'a') : *c;
        } else {
            if (w >= 1) {
                word[w] = 0;
                if (require_in_q && !(w >= 3 && word_in_q(q, word))) { w = 0; if (!*c) break; continue; }
                for (int L = 1; L <= w && L <= 12; L++) {
                    static pk_state F;
                    pk_open(&F, &ST, "");        /* no shortlist: this measures the filter alone */
                    int r = -1, keys = 0, ok = 1;
                    for (int i = 0; i < L; i++) { pk_key(&F, &ST, word[i], &r); keys++; }
                    if (F.level != PK_RECORD) ok = 0;
                    int row = -1;
                    if (ok) { for (int i = 0; i < F.nhit; i++) if (F.hit[i] == rec) { row = i; break; } }
                    if (row < 0) ok = 0;
                    if (ok) {
                        int walk = walk_to(&F, row);
                        if (walk < 0) ok = 0;
                        else {
                            keys += walk;
                            if (pk_key(&F, &ST, K_ENTER, &r) != PK_ACT_PICKED || r != rec) ok = 0;
                            else keys += 1;
                        }
                    }
                    if (ok && (best < 0 || keys < best)) best = keys;
                }
            }
            w = 0;
            if (!*c) break;
        }
    }
    return best;
}

int main(int argc, char **argv) {
    if (ns_load(&ST, argc > 1 ? argv[1] : "build/store.tns") != NS_OK) return 2;
    if (argc > 2) pk_set_max_sug(atoi(argv[2]));
    char line[2048];
    printf("q_chars\tbrowse\tsug\tfilter\tplaus\tin_sug\n");
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
        if (kb < 0) {
            /* TAB out of the shortlist first -- one key -- then browse. That is the shipped route
             * for a miss and it is what the legend tells the student to do. */
            pk_open(&B, &ST, q);
            int rr = -1;
            pk_key(&B, &ST, K_TAB, &rr);
            int rest = browse_to(&B, rec, B.nsug + fam);
            kb = rest < 0 ? -1 : rest + 1;
        }
        if (ka < 0 || kb < 0) continue;

        int kf = filter_cost(q, rec, 0);      /* oracle: any word of the record name  */
        int kp = filter_cost(q, rec, 1);      /* plausible: only words in the question */
        int typing = (int)strlen(q) + 1;          /* the question, then enter */
        printf("%d\t%d\t%d\t%d\t%d\t%d\n", typing, ka, kb, kf, kp, in_sug);
    }
    return 0;
}
