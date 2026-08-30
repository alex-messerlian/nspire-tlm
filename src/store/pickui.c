#include <string.h>
#include "pickui.h"

static int MAX_SUG = PK_MAX_SUG;
void pk_set_max_sug(int k) { MAX_SUG = (k < 0) ? 0 : (k > PK_MAX_SUG ? PK_MAX_SUG : k); }

/* Model rows at the family level: the suggestions, then every family. */
static int fam_rows(const pk_state *p) { return p->nsug + p->nfam; }

int pk_row_is_sug(const pk_state *p, int row) { return row >= 0 && row < p->nsug; }
int pk_row_family(const pk_state *p, int row) {
    int f = row - p->nsug;
    return (f >= 0 && f < p->nfam) ? f : -1;
}

static void clamp(pk_state *p) {
    int n = (p->level == PK_FAMILY) ? fam_rows(p) : p->nhit;
    if (p->sel >= n) p->sel = n ? n - 1 : 0;
    if (p->sel < 0) p->sel = 0;
    int rows = p->rows > 0 ? p->rows : PK_ROWS;
    if (p->sel < p->scroll) p->scroll = p->sel;
    if (p->sel >= p->scroll + rows) p->scroll = p->sel - rows + 1;
    if (p->scroll > n - rows) p->scroll = n - rows;
    if (p->scroll < 0) p->scroll = 0;
}

/* Recompute the candidate set from the query STRING rather than mutating it incrementally.
 * That is what makes backspace reversible for free: there is no undo stack to get wrong,
 * because the set is a pure function of (scope, query). */
static void refilter(pk_state *p, const ns_store2 *st) {
    p->nhit = ns_filter(st, p->fam, p->q, p->hit, NS_MAX_RECORDS);
    if (p->nhit < 0) p->nhit = 0;
    p->sel = 0; p->scroll = 0;
}

void pk_open(pk_state *p, const ns_store2 *st, const char *question) {
    memset(p, 0, sizeof *p);
    p->level = PK_FAMILY;
    p->fam   = -1;
    p->nfam  = ns_families(st, p->fams, NS_MAX_FAMILIES);
    if (p->nfam < 0) p->nfam = 0;

    /* THE SHORTLIST, not a winner. ASK_QTY because it is the measured best of the three -- and
     * the reason it is only a shortlist is measured too: on the 148 items whose record is still
     * in the store, 31.8% share NO content word with their own record's name or its quantity, so
     * no lexical ranker reaches them at all. See docs/RESULT_RETRIEVAL_CEILING.md. */
    if (question && question[0]) {
        static ns_ask a;
        ask_parse(question, &a);
        p->nsug = MAX_SUG ? ask_rank(st, question, &a.in, ASK_QTY, p->sug, MAX_SUG) : 0;
        if (p->nsug < 0) p->nsug = 0;
    }

    /* Two section headers cost 10 px each out of the row area, and the "N more below" line needs
     * its own 9 -- reserving ONE row for all three put "1 more below" through the key legend.
     * Measured against draw_picker's geometry: 40 + 10 + 5*13 + 10 + 6*13 = 203, legend rule at
     * 219. With no suggestions there are no headers and the layout is what it was. */
    p->rows = p->nsug ? PK_ROWS - 2 : PK_ROWS;

    /* TAB LEAVES THE SHORTLIST IN ONE KEY, and it lands on the family the ranker predicts.
     *
     * Measured, and this is why it exists: with a shortlist of five, a student whose record is NOT
     * in it walks 19 selection keys against 15 with no shortlist present. Against a student who
     * would otherwise FILTER -- the realistic case, available on 56.1% of items -- that made
     * Suggested a wash: +2 median but -0.6 mean and a WORSE p90. The whole cost was the walk past
     * five rows. One key removes it, and the destination is the ranker's own top family, which is
     * exactly where the pre-Suggested build put the cursor. */
    if (p->nsug) {
        int f = ns_family_of(st, p->sug[0]);
        p->browse_row = p->nsug + ((f >= 0 && f < p->nfam) ? f : 0);
    }

    /* The cursor starts on the top suggestion when there is one -- that is the row most likely to
     * be wanted -- and on the first family otherwise. A wrong start costs arrow presses, nothing
     * more: nothing downstream depends on it. */
    p->sel = 0;
    clamp(p);
}

int pk_is_empty_search(const pk_state *p) {
    return p->level == PK_RECORD && p->nhit == 0;
}

static void enter_records(pk_state *p, const ns_store2 *st, int fam) {
    p->level = PK_RECORD;
    p->fam   = fam;
    p->q[0] = 0; p->qn = 0;
    refilter(p, st);
}

pk_action pk_key(pk_state *p, const ns_store2 *st, int key, int *out_rec) {
    if (out_rec) *out_rec = -1;

    if (p->level == PK_FAMILY) {
        if (key == K_DOWN) { p->sel++; clamp(p); return PK_ACT_NONE; }
        if (key == K_UP)   { p->sel--; clamp(p); return PK_ACT_NONE; }
        /* TAB jumps between the two sections. Only meaningful when a shortlist is present; with
         * none it is inert rather than doing something arbitrary. */
        if (key == K_TAB && p->nsug) {
            p->sel = pk_row_is_sug(p, p->sel) ? p->browse_row : 0;
            clamp(p);
            return PK_ACT_NONE;
        }
        /* ESC AT THE TOP IS "ASK ANYWAY", not "quit". Every exit that is not a picked record
         * takes the Form C path -- measured 100.0% refused, 0.0% confident answers on 88 real
         * questions the store cannot serve, against 37.5% fabrication when the record span is
         * simply omitted. The student is never stranded. */
        if (key == K_ESC)  return PK_ACT_ASK_ANYWAY;
        if (key == K_ENTER) {
            /* A SUGGESTION IS A RECORD, so enter on one picks it outright -- that is the whole
             * point of the section: one keypress instead of a browse. */
            if (pk_row_is_sug(p, p->sel)) {
                if (out_rec) *out_rec = p->sug[p->sel];
                return PK_ACT_PICKED;
            }
            int f = pk_row_family(p, p->sel);
            if (f >= 0 && p->fams[f].count > 0) enter_records(p, st, f);
            return PK_ACT_NONE;      /* an empty family is not openable; it cannot be entered */
        }
        /* '/' is the documented key. A letter does the same thing, because a student who types
         * "hooke" at the family screen and sees nothing happen has been told the tool is broken.
         * Both scope to ALL records: the family list is a browse path, never a mandatory gate. */
        if (key == '/') { enter_records(p, st, -1); return PK_ACT_NONE; }
        if (key >= 32 && key < 127) {
            enter_records(p, st, -1);
            p->q[p->qn++] = (char)key; p->q[p->qn] = 0;
            refilter(p, st);
            return PK_ACT_NONE;
        }
        return PK_ACT_NONE;
    }

    if (p->level == PK_RECORD) {
        /* ESC GOES UP EXACTLY ONE LEVEL, ALWAYS. Never two, never straight to the top. */
        if (key == K_ESC) {
            p->level = PK_FAMILY;
            p->q[0] = 0; p->qn = 0;
            p->sel = (p->fam >= 0) ? p->nsug + p->fam : 0;
            p->scroll = 0; clamp(p);
            return PK_ACT_NONE;
        }
        if (key == K_DOWN) { p->sel++; clamp(p); return PK_ACT_NONE; }
        if (key == K_UP)   { p->sel--; clamp(p); return PK_ACT_NONE; }
        if (key == K_ENTER) {
            if (p->nhit > 0 && p->sel < p->nhit) { if (out_rec) *out_rec = p->hit[p->sel];
                                                   return PK_ACT_PICKED; }
            return PK_ACT_NONE;
        }
        if (key == K_BACK) {
            if (p->qn) { p->q[--p->qn] = 0; refilter(p, st); }
            return PK_ACT_NONE;      /* backspace EDITS; esc is the way up. One key, one job. */
        }
        /* On the empty-search screen 'a' is "ask anyway", per the spec's own key legend. It costs
         * nothing as a query character there: the set is already empty, so extending the query
         * can only keep it empty. Everywhere else 'a' types. */
        if (key == 'a' && p->nhit == 0) return PK_ACT_ASK_ANYWAY;
        if (key >= 32 && key < 127 && p->qn < PK_QMAX - 1) {
            p->q[p->qn++] = (char)key; p->q[p->qn] = 0;
            refilter(p, st);
        }
        return PK_ACT_NONE;
    }
    return PK_ACT_NONE;
}
