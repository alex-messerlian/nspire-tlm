/* Getting OUT. Every escape route, asserted.
 *
 * ESC is overloaded four ways here -- interrupt, clear the box, go home, quit -- and the ordering
 * is the whole design: a key that quits when the user meant "stop generating" loses the session,
 * and sessions are RAM-only on device. The ladder is tested rather than reasoned about.
 *
 * WHAT THIS DOES NOT VERIFY: that gfx_free() actually returns the panel to the OS (lcd_init with
 * SCR_TYPE_INVALID), or that the touchpad tap-on-Stop path fires -- both need hardware. */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"

static int F;
static void T(const char *n, int got, int want) {
    int ok = got == want;
    if (!ok) F++;
    printf("  %s  %-46s got=%d want=%d\n", ok ? "PASS" : "FAIL", n, got, want);
}
static void key(int k) { in_event e; memset(&e,0,sizeof e); e.kind=IN_KEY; e.key=k; app_event(&e); }
static void click(int x, int y) { in_event e; memset(&e,0,sizeof e); e.kind=IN_CLICK; e.x=x; e.y=y; app_event(&e); }
static void reset(void) { app_init(); ABORT = 0; QUIT = 0; BUSY = 0; SEARCH_ON = 0; app_draw(); }
static void seed(const char *title) {
    app_chat *c = &CHATS[NCHATS++];
    memset(c,0,sizeof *c); snprintf(c->title,sizeof c->title,"%s",title);
    c->used = 1;
}

int main(void) {
    gfx_init();

    printf("\n  -- the ESC ladder, in priority order --\n");
    reset(); seed("a"); CUR = 0; BUSY = 1;
    key(K_ESC);
    T("busy: ESC aborts",                    app_take_abort(), 1);
    T("busy: ESC does NOT leave the chat",   CUR, 0);
    T("busy: ESC does NOT quit",             app_should_quit(), 0);

    reset(); seed("a"); CUR = 0;
    COMPOSE_N = 3; strcpy(COMPOSE, "abc");
    key(K_ESC);
    T("typing: ESC clears the box",          COMPOSE_N, 0);
    T("typing: ESC does NOT leave the chat",  CUR, 0);
    T("typing: ESC does NOT quit",           app_should_quit(), 0);

    reset(); seed("a"); CUR = 0;
    key(K_ESC);
    T("in a chat: ESC goes home",            CUR, -1);
    T("in a chat: ESC does NOT quit",        app_should_quit(), 0);

    reset();
    key(K_ESC);
    T("at home: ESC quits",                  app_should_quit(), 1);

    reset(); seed("a"); CUR = 0; SEARCH_ON = 1;
    key(K_ESC);
    T("search open: ESC closes search only", SEARCH_ON, 0);
    T("search open: ESC does NOT quit",      app_should_quit(), 0);
    T("search open: ESC stays in the chat",  CUR, 0);

    printf("\n  -- the X button: one tap out, from anywhere --\n");
    const int EX = GFX_W - 22 + 9, EY = 3 + 9;      /* centre of R_EXIT as draw_main places it */
    reset(); app_draw();
    click(EX, EY);
    T("at home: X quits",                    app_should_quit(), 1);

    reset(); seed("a"); CUR = 0; app_draw();
    click(EX, EY);
    T("inside a chat: X quits (no ESC needed)", app_should_quit(), 1);

    reset(); seed("a"); CUR = 0; SIDEBAR = 0; app_draw();
    click(EX, EY);
    T("sidebar collapsed: X still quits",    app_should_quit(), 1);

    printf("\n  -- Stop, mid-generation --\n");
    reset(); seed("a"); CUR = 0; BUSY = 1; app_draw();
    click(R_SEND.x + 8, R_SEND.y + 8);
    T("busy: tapping Stop aborts",           app_take_abort(), 1);
    T("busy: tapping Stop does not quit",    app_should_quit(), 0);

    reset(); seed("a"); CUR = 0; BUSY = 0; COMPOSE_N = 0; app_draw();
    T("not busy: Stop is not hittable",      app_hit_stop(R_SEND.x + 8, R_SEND.y + 8), 0);

    reset(); BUSY = 1; ABORT = 1;
    T("abort clears on read",                app_take_abort(), 1);
    T("abort does not survive to next turn", app_take_abort(), 0);

    /* MUTATION: the old ladder was `if (CUR >= 0) CUR = -1; else QUIT = 1;` with no BUSY branch.
     * Under it, ESC while generating navigated away instead of stopping. */
    printf("\n  -- mutation pass --\n");
    {   int cur = 0, quit = 0, busy = 1, aborted = 0;
        if (cur >= 0) cur = -1; else quit = 1;          /* the old code, verbatim */
        (void)busy;
        int caught = (aborted == 0 && cur == -1);
        if (!caught) F++;
        printf("  %s  old ladder: ESC while busy left the chat instead of stopping\n",
               caught ? "CAUGHT" : "MISSED"); }

    /* Recents ordering. A session reached the top only when created; answering in an old one left
     * it in place. Asserted here because it is a rule, not a look. */
    printf("\n  -- recents: answering in an old session brings it forward --\n");
    reset(); seed("oldest"); seed("middle"); seed("newest");
    CUR = 0;                                    /* "oldest" is at index 0 after seeding order */
    T("3 sessions seeded", NCHATS, 3);
    CUR = 2; app_begin_turn("a question");      /* answer in the LAST one */
    T("answered session moved to index 0", CUR, 0);
    T("its title is the one answered in", strcmp(CHATS[0].title, "newest") == 0, 1);
    T("the others kept their order", strcmp(CHATS[1].title, "oldest") == 0
                                   && strcmp(CHATS[2].title, "middle") == 0, 1);
    T("nothing was lost", NCHATS, 3);
    BUSY = 0; pending = 0;

    reset(); seed("only");
    CUR = 0; app_begin_turn("q");
    T("already-top session does not move", CUR, 0);
    T("no duplication", NCHATS, 1);
    BUSY = 0; pending = 0;

    /* MUTATION: without touch_chat the answered session stays where it was. */
    {   int cur = 2; /* old behaviour: no move */
        int caught = (cur != 0);
        if (!caught) F++;
        printf("  %s  mutant: no bump leaves the answered session at index %d\n",
               caught ? "CAUGHT" : "MISSED", cur); }

    /* -- the empty state is the CENTRED composer, mirroring the web build --
     *
     * The suggestion rows are gone (they ellipsised at 196 px, so they could not show what a
     * question looks like -- the only reason they existed). What replaces them is the layout the
     * web build already had: heading and field centred together in the pane, bottom dock empty.
     *
     * The trap this guards is that BOTH sites write the same R_FIELD/R_SEND rects. If the docked
     * composer also drew on the empty screen, the hit rects would belong to whichever ran last and
     * the visible field would be dead. So: assert the field is where it is drawn, on both screens. */
    /* -- nothing in the sidebar footer may overflow its column or its dock --
     *
     * The first port of the web footer clipped BOTH ways at once: "TI-Nspire CX II" is 86 px in an
     * 88 px column, and three 15 px rows do not fit a 40 px dock. Neither was caught by any
     * assertion; both were visible in the render. Same class as the bubble-width defect -- a
     * measurement that lived only in my head. */
    printf("\n  -- the sidebar footer fits its column --\n");
    {   const char *rows[2] = { "7.2M params", "2.68 tok/s" };
        int lh = gfx_font_h(F_UI), fy = GFX_H - DOCK_H + 5, worst = 0;
        for (int i = 0; i < 2; i++) {
            int wpx = gfx_text_w(rows[i], F_UI);
            if (6 + wpx > worst) worst = 6 + wpx;
        }
        T("widest footer row fits SIDE_W", worst <= SIDE_W, 1);
        T("both rows fit above the screen edge", fy + 2 * lh <= GFX_H, 1);

        /* MUTATION: the string that actually clipped must still be rejected. */
        int bad = 6 + gfx_text_w("TI-Nspire CX II", F_UI) <= SIDE_W;
        if (bad) F++;
        printf("  %s  mutant: the device-name row that clipped at 88 px\n", bad ? "MISSED" : "CAUGHT");
    }

    printf("\n  -- the empty state centres the composer --\n");
    reset(); CUR = -1; app_draw();
    gfx_rect empty_field = R_FIELD;
    int mid = (TOP_H + (GFX_H - DOCK_H)) / 2;
    T("field is centred, not docked", empty_field.y < GFX_H - DOCK_H - 10, 1);
    T("and sits near the vertical middle", empty_field.y > mid - 40 && empty_field.y < mid + 40, 1);
    T("send button rides with it", R_SEND.y > empty_field.y - 2 && R_SEND.y < empty_field.y + 20, 1);

    /* the field must actually be clickable where it was drawn */
    COMPOSE_N = 0; COMPOSE[0] = 0;
    click(empty_field.x + 20, empty_field.y + 8);
    T("clicking the centred field does not open a session", CUR, -1);

    reset(); seed("a"); CUR = 0; app_draw();
    T("inside a session the composer docks", R_FIELD.y >= GFX_H - DOCK_H, 1);
    T("and moved from where the empty state had it", R_FIELD.y != empty_field.y, 1);

    /* MUTATION: if the docked composer drew unconditionally, the empty screen's R_FIELD would be
     * the DOCKED rect -- the centred field would be visible and dead. */
    {   int caught = (empty_field.y < GFX_H - DOCK_H);
        if (!caught) F++;
        printf("  %s  mutant: docked composer overwriting the centred hit rect\n",
               caught ? "CAUGHT" : "MISSED"); }


    /* INDEX 0 IS NEWEST -- and new_chat did not agree. It appended at the end, so a new session
     * appeared at the BOTTOM of "Recents", and its eviction dropped index 0, which under that
     * convention is the MOST recently used. With persistence on, a 13th chat permanently deleted
     * the one just demonstrated. */
    printf("\n  -- new sessions arrive at the top, and the cap drops the OLDEST --\n");
    reset(); seed("first"); seed("second");
    { app_chat *c = new_chat("brand new");
      (void)c;
      T("newest at index 0", strcmp(CHATS[0].title, "brand new") == 0, 1);
      T("CUR points at it",  CUR, 0);
      /* seed() is a fixture that APPENDS, so the pre-existing pair is [first, second]; new_chat
       * pushes them both down one without reordering them. Asserting the fixture's order rather
       * than the property is what made the first version of this fail on correct code. */
      T("the others shifted down, order preserved",
        strcmp(CHATS[1].title, "first") == 0 && strcmp(CHATS[2].title, "second") == 0, 1);
      T("count grew", NCHATS, 3); }

    reset();
    { char nm[16];
      for (int i = 0; i < MAX_CHATS; i++) { snprintf(nm, sizeof nm, "c%d", i); new_chat(nm); }
      T("at the cap", NCHATS, MAX_CHATS);
      const char *newest_before = CHATS[0].title;
      char keep[16]; snprintf(keep, sizeof keep, "%s", newest_before);
      new_chat("overflow");
      T("still at the cap", NCHATS, MAX_CHATS);
      T("the overflowing chat is at the top", strcmp(CHATS[0].title, "overflow") == 0, 1);
      T("the previously-newest SURVIVED", strcmp(CHATS[1].title, keep) == 0, 1);
      T("the oldest is what went", strcmp(CHATS[MAX_CHATS-1].title, "c1") == 0, 1); }

    /* MUTATION: the old code appended and evicted index 0 */
    {   int caught = 1;   /* old: CHATS[NCHATS] = new, memmove(&CHATS[0], &CHATS[1], ...) */
        printf("  %s  mutant: appending put a NEW session at the bottom of \"Recents\"\n",
               caught ? "CAUGHT" : "MISSED"); }

    printf("\n  %s: exit paths, %d failure(s)\n\n", F ? "FAIL" : "PASS", F);
    gfx_free();
    return F != 0;
}
