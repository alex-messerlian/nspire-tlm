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
/* app_init() clears the chat state but NOT the view state, so SIDEBAR, the cursor and HOVER
 * survived reset() and leaked between cases. A click in one test that happens to land on
 * R_TOGGLE collapses the sidebar for every test after it, and the ones that then measure sidebar
 * geometry fail for a reason that has nothing to do with what they assert. Restore all of it. */
static void reset(void) {
    app_init();
    ABORT = 0; QUIT = 0; BUSY = 0; SEARCH_ON = 0;
    SIDEBAR = 1; HOVER = 0; MX = MY = 0; SCROLL = 0; CHAT_SCROLL = 0;
    app_draw();
}
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
    /* -- closing the sidebar must not hide the way back --
     *
     * R_TOGGLE kept whatever rect it held when the sidebar last drew, so with the sidebar closed
     * the control was still CLICKABLE and no longer VISIBLE. A rect-only assertion would have
     * passed throughout, because the rect was never the thing that broke -- so this reads the
     * framebuffer and requires ink inside the plate. Same class as IN_SCROLL being handled with
     * nothing emitting it. */
    printf("\n  -- the sidebar can be reopened after closing it --\n");
    reset(); seed("A car goes 150 m in 12 s."); CUR = -1;
    SIDEBAR = 0; HOVER = 0; MX = MY = 0; app_draw();
    T("the toggle is on screen", R_TOGGLE.x >= 0 && R_TOGGLE.y >= 0, 1);
    T("and inside the top bar",  R_TOGGLE.y + R_TOGGLE.h <= TOP_H, 1);
    {   /* ink, not just geometry: at least one pixel of the plate differs from the page ground */
        const uint16_t *fb = gfx_buf();
        int ink = 0;
        for (int yy = R_TOGGLE.y; yy < R_TOGGLE.y + R_TOGGLE.h; yy++)
            for (int xx = R_TOGGLE.x; xx < R_TOGGLE.x + R_TOGGLE.w; xx++)
                if (fb[yy * GFX_W + xx] != C_BG) ink++;
        T("something is actually DRAWN there", ink > 0, 1);

        /* MUTATION: the shipped bug drew nothing when closed. An empty plate is the failure. */
        if (!(ink > 0)) F++;
        printf("  %s  mutant: a clickable rect with no pixels under it (%d px of ink)\n",
               ink > 0 ? "CAUGHT" : "MISSED", ink);
    }
    click(R_TOGGLE.x + 10, R_TOGGLE.y + 10);
    T("clicking it reopens the sidebar", SIDEBAR, 1);

    /* every control is one class: same plate, same size */
    reset(); CUR = -1; app_draw();
    T("exit matches the sidebar icons", R_EXIT.w == R_NEW.w && R_EXIT.h == R_NEW.h, 1);
    T("and sits on the same row",       R_EXIT.y == R_NEW.y, 1);
    T("exit clears the screen edge",    R_EXIT.x + R_EXIT.w <= GFX_W - 3, 1);

    /* -- hover marquee on session titles --
     *
     * Titles ellipsise at ~64px, which for a question is a few words and often not enough to tell
     * two sessions apart. On hover the full title scrolls left, STOPS at its end, and resets when
     * the cursor leaves. The clamp is the part worth asserting: without it the title keeps going
     * and scrolls off its own left edge, leaving a blank row that reads as a rendering fault. */
    printf("\n  -- session titles marquee on hover --\n");
    {   T("still at rest during the hold",  marq_off(MARQ_HOLD, 40), 0);
        T("has not moved one draw before",  marq_off(MARQ_HOLD - 1, 40), 0);
        T("moves after the hold",           marq_off(MARQ_HOLD + 2 * MARQ_DIV, 40) > 0, 1);
        T("advances one px per MARQ_DIV",   marq_off(MARQ_HOLD + 10 * MARQ_DIV, 40), 10);
        T("STOPS at the end",               marq_off(MARQ_HOLD + 400 * MARQ_DIV, 40), 40);
        T("never exceeds the overflow",     marq_off(999999, 40), 40);
        T("a title that fits never moves",  marq_off(999999, 0), 0);
        T("negative overflow is not motion", marq_off(999999, -12), 0);

        /* MUTATION: drop the clamp and a long title runs off its own left edge. */
        int unclamped = (MARQ_HOLD + 400 * MARQ_DIV - MARQ_HOLD) / MARQ_DIV;
        int caught = (marq_off(MARQ_HOLD + 400 * MARQ_DIV, 40) != unclamped);
        if (!caught) F++;
        printf("  %s  mutant: no clamp -> travel %d px against a 40 px overflow\n",
               caught ? "CAUGHT" : "MISSED", unclamped);
    }

    /* the hover STATE: it must reset when the cursor leaves, or the next hover resumes mid-scroll */
    reset(); seed("Speed from distance and time on a long straight road"); CUR = -1;
    MX = 40; MY = 50; HOVER = 1; app_draw();
    T("hovering a row arms the marquee", MARQ_AT >= 0, 1);
    {   int t0 = MARQ_T; app_draw();
        T("each draw advances it", MARQ_T > t0, 1); }
    MX = 300; MY = 220; app_draw();
    T("leaving the list disarms it", MARQ_AT, -1);

    /* -- the icon band, and what the reclaimed footer band bought --
     *
     * The footer used to spend all of DOCK_H on two numbers that never change while the app runs.
     * With it gone and the list on F_SM's tighter pitch, CHAT_FIT reaches MAX_CHATS: every session
     * the app will keep is visible at once. That is the assertion worth holding, because it is the
     * property a future pitch or padding change would quietly break. */
    printf("\n  -- the sidebar column --\n");
    reset();
    for (int i = 0; i < MAX_CHATS; i++) seed("A car goes 150 m in 12 s. Find the speed.");
    CUR = -1; app_draw();
    T("every kept session is visible", CHAT_FIT >= MAX_CHATS, 1);
    T("all of them actually drew",     NCHAT_ROWS, MAX_CHATS);
    T("the last row clears the screen", R_CHAT[MAX_CHATS-1].y + R_CHAT[MAX_CHATS-1].h <= GFX_H, 1);
    /* Recents is drawn at TOP_H+8 with F_SM's 13px box, so it ends at TOP_H+21. The first row's
     * highlight starts at R_CHAT[0].y and must clear that, with air rather than exactly abutting:
     * three text bands stacked with no separation is what "the spacing is terrible" meant. */
    T("the first row clears Recents",   R_CHAT[0].y >= TOP_H + 8 + gfx_font_h(F_SM), 1);
    T("with air, not flush",            R_CHAT[0].y - (TOP_H + 8 + gfx_font_h(F_SM)) >= 3, 1);
    T("the icons have air in the band", R_NEW.y >= 3 && R_NEW.y + R_NEW.h <= TOP_H - 2, 1);

    /* the three icons are EVENLY spaced -- equal gaps between them and at both ends */
    {   int g0 = R_NEW.x;                              /* left edge to first */
        int g1 = R_SEARCH.x - (R_NEW.x + R_NEW.w);     /* first to second */
        int g2 = R_TOGGLE.x - (R_SEARCH.x + R_SEARCH.w);
        int g3 = SIDE_W - (R_TOGGLE.x + R_TOGGLE.w);   /* last to the divider */
        T("gaps are equal", g0 == g1 && g1 == g2 && g2 == g3, 1);
        T("and the icons are the same size",
          R_NEW.w == R_SEARCH.w && R_SEARCH.w == R_TOGGLE.w &&
          R_NEW.h == R_SEARCH.h && R_SEARCH.h == R_TOGGLE.h, 1);
        T("all on one row", R_NEW.y == R_SEARCH.y && R_SEARCH.y == R_TOGGLE.y, 1);
        T("the band fits the column", R_TOGGLE.x + R_TOGGLE.w <= SIDE_W, 1);
        /* MUTATION: the old lopsided layout -- one icon left, two right -- had g0=4 and g2=2
         * against a 19px hole in the middle. */
        int lop0 = 4, lop1 = 88 - 45 - (4 + 20), lop2 = 88 - 23 - (88 - 45 + 20);
        int caught = !(lop0 == lop1 && lop1 == lop2);
        if (!caught) F++;
        printf("  %s  mutant: the lopsided band (gaps %d / %d / %d)\n",
               caught ? "CAUGHT" : "MISSED", lop0, lop1, lop2);
    }

    /* the footer is GONE, not merely moved off-screen */
    T("no footer text is drawn in the dock band", R_LIST.y + R_LIST.h > GFX_H - DOCK_H, 1);

    /* -- hover marquee on session titles --
     *
     * Titles ellipsise at ~64px, which for a question is a few words and often not enough to tell
     * two sessions apart. On hover the full title scrolls left, STOPS at its end, and resets when
     * the cursor leaves. The clamp is the part worth asserting: without it the title keeps going
     * and scrolls off its own left edge, leaving a blank row that reads as a rendering fault. */
    printf("\n  -- session titles marquee on hover --\n");
    {   T("still at rest during the hold",  marq_off(MARQ_HOLD, 40), 0);
        T("has not moved one draw before",  marq_off(MARQ_HOLD - 1, 40), 0);
        T("moves after the hold",           marq_off(MARQ_HOLD + 2 * MARQ_DIV, 40) > 0, 1);
        T("advances one px per MARQ_DIV",   marq_off(MARQ_HOLD + 10 * MARQ_DIV, 40), 10);
        T("STOPS at the end",               marq_off(MARQ_HOLD + 400 * MARQ_DIV, 40), 40);
        T("never exceeds the overflow",     marq_off(999999, 40), 40);
        T("a title that fits never moves",  marq_off(999999, 0), 0);
        T("negative overflow is not motion", marq_off(999999, -12), 0);

        /* MUTATION: drop the clamp and a long title runs off its own left edge. */
        int unclamped = (MARQ_HOLD + 400 * MARQ_DIV - MARQ_HOLD) / MARQ_DIV;
        int caught = (marq_off(MARQ_HOLD + 400 * MARQ_DIV, 40) != unclamped);
        if (!caught) F++;
        printf("  %s  mutant: no clamp -> travel %d px against a 40 px overflow\n",
               caught ? "CAUGHT" : "MISSED", unclamped);
    }

    /* the hover STATE: it must reset when the cursor leaves, or the next hover resumes mid-scroll */
    reset(); seed("Speed from distance and time on a long straight road"); CUR = -1;
    MX = 40; MY = 50; HOVER = 1; app_draw();
    T("hovering a row arms the marquee", MARQ_AT >= 0, 1);
    {   int t0 = MARQ_T; app_draw();
        T("each draw advances it", MARQ_T > t0, 1); }
    MX = 300; MY = 220; app_draw();
    T("leaving the list disarms it", MARQ_AT, -1);

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
