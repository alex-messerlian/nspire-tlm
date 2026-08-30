#ifndef NS_PICKUI_H
#define NS_PICKUI_H
#include "loader.h"
#include "picker.h"
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
 * scrolling -- test_pickui asserts that, and it would fail if this dropped to 12. */
#define PK_ROWS      13
#define PK_QMAX      40

typedef enum { PK_OFF = 0, PK_FAMILY, PK_RECORD } pk_level;

/* What the caller must do after a key. PK_NONE means the picker consumed it. */
typedef enum { PK_ACT_NONE = 0, PK_ACT_PICKED, PK_ACT_ASK_ANYWAY } pk_action;

typedef struct {
    int  level;                      /* pk_level                                              */
    int  fam;                        /* family in scope; -1 means "searching all records"     */
    char q[PK_QMAX];
    int  qn;
    int  hit[NS_MAX_RECORDS];
    int  nhit;
    int  sel, scroll;
    ns_family fams[NS_MAX_FAMILIES];
    int  nfam;
} pk_state;

void      pk_open(pk_state *p, const ns_store2 *st, int preselect_fam);
pk_action pk_key(pk_state *p, const ns_store2 *st, int key, int *out_rec);
/* True when the record list is showing a search that matched nothing -- a first-class screen
 * carrying the base rate, not an error. 44% of real textbook questions have no matching record. */
int       pk_is_empty_search(const pk_state *p);
#endif
