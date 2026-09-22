/* The character palette: the tiles themselves, and the focus chain that reaches them.
 *
 * WHY THIS SUITE DID NOT EXIST BEFORE AND DOES NOW. The palette was one flat array of sixteen
 * strings with no state beyond a selection index, so there was nothing to get wrong. It is now five
 * categories behind a tab row, which adds exactly the shape this repo keeps paying for: TWO places
 * that must agree about which list is on screen. draw_symbols indexes it, the key handler indexes
 * it, and the touch handler indexes it. `PICK_ON` and `pk_open` already disagreed once about
 * ranking for the same reason.
 *
 * So every check below asks the question through pal_at()/pal_n() -- the single accessor -- and
 * then asserts the VISIBLE consequence, because an accessor both sides call is only a fix if both
 * sides actually call it.
 *
 * The clamp is the one worth naming. GREEK holds 20 and CONST holds 15, so a cursor parked on the
 * last GREEK tile and tabbed twice indexes past the end of CONST. That is a read out of bounds in
 * a UI where the reward for it is a garbage glyph rather than a crash, which is how it would have
 * shipped.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"

static int F;
#define OK(cond, ...) do { int _c = !!(cond); if (!_c) F++; \
    printf("  %s  ", _c ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); } while (0)

static void key(int k) { in_event e = { .kind = IN_KEY, .key = k }; app_event(&e); }
static void tap(int x, int y) {
    MX = x; MY = y;
    in_event e = { .kind = IN_CLICK, .x = x, .y = y }; app_event(&e);
}
static void open_pal(void) {
    compose_clear(); SYM_ON = 0; SYM_CAT = 0; SYM_SEL = 0;
    CUR = -1; FIELD_FOCUS = 1;
    key(K_SYM);
}

int main(void) {
    gfx_init();
    app_init();

    /* ---- 1. the tables themselves ------------------------------------------------------ */
    OK(CAT_N >= 2, "CAT_N = %d categories", CAT_N);
    int total = 0;
    for (int c = 0; c < CAT_N; c++) {
        const pal_cat *p = &PAL_CAT[c];
        total += p->n;
        OK(p->n > 0 && p->n <= SYM_MAX, "%-5s holds %d tiles (max %d)", p->name, p->n, SYM_MAX);
        OK(p->name && p->name[0], "%-5s has a name", p->name ? p->name : "(null)");
        /* A122. THE TWO STRINGS ARE NOW DIFFERENT THINGS AND EACH GETS ITS OWN RULE.
         *
         *   it[i]  is INSERTED into the question. It must be ASCII, for the reason below, and its
         *          LENGTH is unconstrained -- it goes into the composer, not into a cell, so
         *          "antiderivative of " is fine and was failing a check meant for the tile.
         *   lab[i] is DRAWN on the tile. It is what must fit, and it may be a glyph as long as the
         *          FONT carries it (U+222B does; test_notation is what catches one that does not).
         *
         * Before CALC the two were one string, so one rule covered both and applying the cell
         * constraint to the insertion was free. It is not free any more. */
        int bad_ascii = 0, empty = 0, toolong = 0;
        for (int i = 0; i < p->n; i++) {
            const char *t = p->it[i];
            const char *l = (p->lab && p->lab[i]) ? p->lab[i] : t;
            if (!t || !t[0] || !l || !l[0]) { empty++; continue; }
            if (strlen(l) > 12) toolong++;
            for (const char *q = t; *q; q++)
                if ((unsigned char)*q > 0x7F) bad_ascii++;
        }
        OK(!empty, "%-5s no empty tile", p->name);
        /* ASCII ONLY, and it is not a style rule. store_clean.json holds zero non-ASCII characters
         * and spells Greek out, so a glyph tile would insert a string the model has never read.
         * gfx_text also draws NOTHING for a glyph the font lacks -- see test_notation. */
        OK(!bad_ascii, "%-5s every tile is ASCII (%d violations)", p->name, bad_ascii);
        OK(!toolong, "%-5s every LABEL fits a cell (%d over 12 chars)", p->name, toolong);
    }
    OK(total >= 60, "%d tiles across %d categories", total, CAT_N);

    /* no duplicate tile inside one category: a second copy is dead space on a small screen */
    for (int c = 0; c < CAT_N; c++) {
        int dup = 0;
        for (int i = 0; i < PAL_CAT[c].n; i++)
            for (int j = i + 1; j < PAL_CAT[c].n; j++)
                if (!strcmp(PAL_CAT[c].it[i], PAL_CAT[c].it[j])) dup++;
        OK(!dup, "%-5s no duplicate tiles (%d)", PAL_CAT[c].name, dup);
    }

    /* ---- 2. the accessor agrees with the table ----------------------------------------- */
    for (int c = 0; c < CAT_N; c++) {
        SYM_CAT = c;
        OK(pal_n() == PAL_CAT[c].n, "pal_n() == %d for %s", PAL_CAT[c].n, PAL_CAT[c].name);
        OK(!strcmp(pal_at(0), PAL_CAT[c].it[0]), "pal_at(0) == %s", PAL_CAT[c].it[0]);
    }
    SYM_CAT = -1; OK(pal_n() == PAL_CAT[0].n, "an out-of-range category falls back to 0");
    SYM_CAT = 999; OK(pal_n() == PAL_CAT[0].n, "an over-range category falls back to 0");

    /* ---- 3. TAB cycles, and the cursor is CLAMPED -------------------------------------- */
    open_pal();
    OK(SYM_ON && SYM_CAT == 0, "menu key opens on category 0");
    for (int c = 1; c <= CAT_N; c++) {
        key(K_TAB);
        OK(SYM_CAT == c % CAT_N, "TAB %d -> category %d", c, SYM_CAT);
    }
    /* park on the last tile of the widest category, then tab to a narrower one */
    int widest = 0;
    for (int c = 1; c < CAT_N; c++) if (PAL_CAT[c].n > PAL_CAT[widest].n) widest = c;
    int narrow = 0;
    for (int c = 1; c < CAT_N; c++) if (PAL_CAT[c].n < PAL_CAT[narrow].n) narrow = c;
    OK(PAL_CAT[widest].n > PAL_CAT[narrow].n,
       "the clamp has something to clamp: %s=%d > %s=%d",
       PAL_CAT[widest].name, PAL_CAT[widest].n, PAL_CAT[narrow].name, PAL_CAT[narrow].n);
    SYM_CAT = widest; SYM_SEL = PAL_CAT[widest].n - 1;
    while (SYM_CAT != narrow) key(K_TAB);
    OK(SYM_SEL < pal_n(), "cursor clamped to %d after tabbing into %s (n=%d)",
       SYM_SEL, PAL_CAT[narrow].name, pal_n());
    OK(SYM_SEL >= 0, "cursor did not go negative");

    /* ---- 4. ENTER inserts from the CURRENT category ------------------------------------ */
    for (int c = 0; c < CAT_N; c++) {
        open_pal();
        for (int i = 0; i < c; i++) key(K_TAB);
        SYM_SEL = 2;
        const char *want = PAL_CAT[c].it[2];
        key(K_ENTER);
        OK(!strcmp(COMPOSE, want), "%-5s ENTER inserted %s (got \"%s\")",
           PAL_CAT[c].name, want, COMPOSE);
        OK(!SYM_ON, "%-5s ENTER closed the sheet", PAL_CAT[c].name);
    }

    /* ---- 5. the tab row is part of the focus chain, not a second mode ------------------- */
    open_pal();
    SYM_SEL = 0;
    key(K_UP);
    OK(SYM_SEL == -1, "UP from the first row parks on the tab row");
    int was = SYM_CAT;
    key(K_RIGHT);
    OK(SYM_CAT == (was + 1) % CAT_N, "RIGHT on the tab row changes category");
    key(K_LEFT);
    OK(SYM_CAT == was, "LEFT on the tab row changes it back");
    key(K_DOWN);
    OK(SYM_SEL == 0, "DOWN returns to the grid");
    key(K_UP); key(K_ENTER);
    OK(SYM_ON && SYM_SEL == 0,
       "ENTER on the tab row enters the grid rather than inserting a tab name");
    OK(!COMPOSE[0], "...and inserted nothing (COMPOSE is \"%s\")", COMPOSE);

    /* ---- 6. a tap on a tab switches; a tap on a tile inserts ---------------------------- */
    open_pal();
    app_draw();                                   /* R_CAT / R_SYM are filled by the draw */
    OK(R_CAT[CAT_N - 1].w > 0, "the last tab has a hit rect after a draw");
    tap(R_CAT[CAT_N - 1].x + 2, R_CAT[CAT_N - 1].y + 2);
    OK(SYM_CAT == CAT_N - 1, "tapping the last tab selects it (SYM_CAT=%d)", SYM_CAT);
    OK(SYM_ON, "tapping a tab does NOT close the sheet");
    app_draw();
    tap(R_SYM[1].x + 2, R_SYM[1].y + 2);
    OK(!strcmp(COMPOSE, PAL_CAT[CAT_N - 1].it[1]),
       "tapping tile 1 of the last category inserted \"%s\"", COMPOSE);
    OK(!SYM_ON, "tapping a tile closed the sheet");

    /* ---- 7. every tile the draw lays out is inside the sheet ---------------------------- */
    for (int c = 0; c < CAT_N; c++) {
        open_pal();
        for (int i = 0; i < c; i++) key(K_TAB);
        app_draw();
        int off = 0;
        for (int i = 0; i < pal_n(); i++)
            if (R_SYM[i].x < 0 || R_SYM[i].y < 0 ||
                R_SYM[i].x + R_SYM[i].w > GFX_W || R_SYM[i].y + R_SYM[i].h > GFX_H) off++;
        OK(!off, "%-5s all %d tiles on screen (%d off)", PAL_CAT[c].name, pal_n(), off);
        /* and the label must fit the cell it is centred in, or it renders over its neighbour */
        int clipped = 0;
        for (int i = 0; i < pal_n(); i++)
            if (gfx_text_w(pal_lab(i), F_UI) > R_SYM[i].w) clipped++;
        OK(!clipped, "%-5s no label wider than its cell (%d)", PAL_CAT[c].name, clipped);
        /* AND THE INSERTION IS WHAT LANDS IN THE QUESTION. A tile that draws correctly and inserts
         * nothing is the shape this split makes newly possible, so it is asserted directly. */
        int noins = 0;
        for (int i = 0; i < pal_n(); i++) if (!pal_at(i) || !pal_at(i)[0]) noins++;
        OK(!noins, "%-5s every tile inserts something (%d empty)", PAL_CAT[c].name, noins);
    }

    /* A126. THE PLACEHOLDER IS A PROMISE TOO, and it belongs in this file for that reason: the
     * tiles and the "Ask me about {topic}" rotation are the two places the app tells a student what
     * it can do. A tile that inserts arithmetic the model cannot emit and a topic that resolves to
     * nothing are the same defect.
     *
     * The old list carried 38 topics of which SEVENTEEN refused -- algebra, trigonometry,
     * logarithms, matrices, sequences, quadratics, exponentials, limits, optimization, averages,
     * distributions, regression, motion, circuits, magnetism among them. A student who followed the
     * prompt and typed "distributions" was refused by the thing that invited them. */
    printf("\n  -- every placeholder topic resolves to a record --\n");
    {
        const ns_store2 *st = app_store();
        if (!st) {
            printf("  SKIP  no store on disk -- NOT a pass\n"); F++;
        } else {
            int miss = 0; char first[96]; first[0] = 0;
            for (int i = 0; i < ASK_N; i++) {
                char q[128]; snprintf(q, sizeof q, "what is %s", ASK_ABOUT[i]);
                static ns_ask a; ask_parse(q, &a);
                int idx = -1;
                if (!ask_confident(st, q, &a.in, &idx) || idx < 0) {
                    if (!first[0]) snprintf(first, sizeof first, "%s", ASK_ABOUT[i]);
                    miss++;
                }
            }
            char m[160];
            snprintf(m, sizeof m, "%d of %d resolve%s%s", ASK_N - miss, ASK_N,
                     miss ? ", first miss: " : "", miss ? first : "");
            OK(!miss, "placeholder topics the app can answer (%s)", m);

            /* A135. Two TRY ONE assertions stood here -- that each example resolves against
             * the store, and that each fits the pane. The examples were removed after device use
             * (a click just below the composer ran one by accident). The same promise is still
             * asserted one screen over, for the placeholder topics, immediately above. */
        }
    }

    printf("%s test_palette: %d failure%s\n", F ? "FAIL" : "PASS", F, F == 1 ? "" : "s");
    return F ? 1 : 0;
}
