#include <string.h>
#include "pickui.h"

static void clamp(pk_state *p) {
    int n = (p->level == PK_FAMILY) ? p->nfam : p->nhit;
    if (p->sel >= n) p->sel = n ? n - 1 : 0;
    if (p->sel < 0) p->sel = 0;
    if (p->sel < p->scroll) p->scroll = p->sel;
    if (p->sel >= p->scroll + PK_ROWS) p->scroll = p->sel - PK_ROWS + 1;
    if (p->scroll > n - PK_ROWS) p->scroll = n - PK_ROWS;
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

void pk_open(pk_state *p, const ns_store2 *st, int preselect_fam) {
    memset(p, 0, sizeof *p);
    p->level = PK_FAMILY;
    p->fam   = -1;
    p->nfam  = ns_families(st, p->fams, NS_MAX_FAMILIES);
    if (p->nfam < 0) p->nfam = 0;
    /* The name-overlap picker still runs, and this is the only thing it decides: which family the
     * cursor starts on. At 9.5% it is not allowed to choose the record -- that is the student's --
     * but starting the cursor near the right answer costs nothing and is better than starting at
     * zero. Nothing downstream depends on it being right. */
    if (preselect_fam >= 0 && preselect_fam < p->nfam) p->sel = preselect_fam;
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
        /* ESC AT THE TOP IS "ASK ANYWAY", not "quit". Every exit that is not a picked record
         * takes the Form C path -- measured 100.0% refused, 0.0% confident answers on 88 real
         * questions the store cannot serve, against 37.5% fabrication when the record span is
         * simply omitted. The student is never stranded. */
        if (key == K_ESC)  return PK_ACT_ASK_ANYWAY;
        if (key == K_ENTER) {
            if (p->sel < p->nfam && p->fams[p->sel].count > 0) enter_records(p, st, p->sel);
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
            p->sel = (p->fam >= 0) ? p->fam : 0;
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
