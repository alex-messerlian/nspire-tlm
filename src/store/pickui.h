#ifndef NS_PICKUI_H
#define NS_PICKUI_H
#include "loader.h"
#include "picker.h"
#include "askparse.h"
#include "app.h"   /* K_UP, K_DOWN, K_ESC, K_ENTER, K_BACK -- the key codes ARE the interface */

/* The picker's STATE MACHINE, separate from its drawing. See docs/PICKER_SPEC.md.
 *
 * Split this way for the reason askparse.c was split: logic that lives inside app.c can only be
 * checked by pushing a .tns and looking at the screen, and the bugs here are navigational --
 * does esc go up exactly one level, does backspace restore the previous candidate set, is an
 * empty result a screen or an error. Those are decidable on the host. app.c draws; this decides.
 *
 * THE PREMISE. Retrieval measured 8.0% on the clean surface and 9.5% on device over 200 labelled
 * questions (tools/eval/askcli). E's answer is not a better ranker -- it is that the student picks
 * the relation, and the runtime then tells the model fit:high by construction. */

/* VISIBLE ROWS, derived from the geometry draw_picker actually uses rather than assumed.
 * PICKER_SPEC says 15; that budget did not include keeping the question on screen, and at 15 the
 * list drew straight through the key legend -- caught by render_app, not by any assertion, which
 * is why the picker was rendered before it was pushed. The arithmetic, in device pixels:
 *
 *   sheet   240 - 8 chrome            = 232
 *   header  title 19 + question 17    =  36
 *   legend  rule + line               =  20
 *   rows    (232 - 36 - 20) / 13      =  13
 *
 * 13 is also exactly the family count, so the family list still fits one screen with no
 * scrolling -- test_pickui asserts that, and it would fail if this dropped to 12.
 *
 * WITH SUGGESTIONS the two section headers cost 9 px each, leaving (176 - 18) / 13 = 12 rows. The
 * page size therefore VARIES, and it is carried in pk_state rather than being a constant each of
 * draw and clamp computes for itself -- two copies of a layout number is how a selected row ends
 * up drawn off the bottom of its own list. */
#define PK_ROWS      13
#define PK_MAX_SUG    5
#define PK_QMAX      40

typedef enum { PK_OFF = 0, PK_FAMILY, PK_RECORD } pk_level;

/* What the caller must do after a key. PK_NONE means the picker consumed it. */
typedef enum { PK_ACT_NONE = 0, PK_ACT_PICKED, PK_ACT_ASK_ANYWAY } pk_action;

typedef struct {
    int  level;                      /* pk_level                                              */
    /* SUGGESTIONS, at the family level only, occupying model rows [0, nsug) above the families.
     * ADDITIVE: with nsug == 0 every row index, key and escape is exactly what it was, which is
     * what keeps the esc ruling untouched. They are a shortlist and are labelled as one -- at a
     * measured @5 the right relation is there about half the time, so a section that LOOKED
     * authoritative would read as confidence the ranker has not earned. */
    int  sug[PK_MAX_SUG];
    int  nsug;
    int  browse_row;                 /* where TAB lands in the family section                 */
    int  rows;                       /* visible rows THIS layout allows; draw and clamp share it */
    int  fam;                        /* family in scope; -1 means "searching all records"     */
    char q[PK_QMAX];
    int  qn;
    int  hit[NS_MAX_RECORDS];
    int  nhit;
    int  sel, scroll;
    ns_family fams[NS_MAX_FAMILIES];
    int  nfam;
} pk_state;

/* `question` is used for the suggestions and for the cursor's start row. Passing the question
 * rather than a precomputed family keeps the ranking in one place. */
void      pk_open(pk_state *p, const ns_store2 *st, const char *question);
/* Model row -> what it is. Rows [0,nsug) are suggestions; the rest are families. */
int       pk_row_is_sug(const pk_state *p, int row);
int       pk_row_family(const pk_state *p, int row);
/* Shortlist size. Settable because it is a TRADE, not a preference: a longer list catches the
 * record more often and costs everyone who misses one arrow press per row. The size was chosen by
 * sweeping it against measured keystrokes (tools/eval/keycost.c), not by picking a round number. */
void      pk_set_max_sug(int k);
pk_action pk_key(pk_state *p, const ns_store2 *st, int key, int *out_rec);
/* True when the record list is showing a search that matched nothing -- a first-class screen
 * carrying the base rate, not an error. 44% of real textbook questions have no matching record. */
int       pk_is_empty_search(const pk_state *p);
#endif
