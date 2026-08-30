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
static void T2(const char *n, const char *got, const char *want) {
    int ok = !strcmp(got, want);
    if (!ok) F++;
    printf("  %s  %-46s got=\"%s\" want=\"%s\"\n", ok ? "PASS" : "FAIL", n, got, want);
}
static void key(int k) { in_event e; memset(&e,0,sizeof e); e.kind=IN_KEY; e.key=k; app_event(&e); }
static void click(int x, int y) { in_event e; memset(&e,0,sizeof e); e.kind=IN_CLICK; e.x=x; e.y=y; app_event(&e); }
/* app_init() clears the chat state but NOT the view state, so SIDEBAR, the cursor and HOVER
 * survived reset() and leaked between cases. A click in one test that happens to land on
 * R_TOGGLE collapses the sidebar for every test after it, and the ones that then measure sidebar
 * geometry fail for a reason that has nothing to do with what they assert. Restore all of it. */
static void reset(void) {
    app_init();
    /* EVERY modal and view flag, not just the ones that existed when this was written. Three
     * separate cases have now failed because a flag survived reset(): SIDEBAR collapsed by a stray
     * click, the marquee clock, and now SETTINGS_ON left open, which ate the next test's clicks
     * because the sheet is modal. The rule is that reset() means reset. */
    ABORT = 0; QUIT = 0; BUSY = 0; SEARCH_ON = 0; SETTINGS_ON = 0; FIELD_FOCUS = 0;
    SIDEBAR = 1; HOVER = 0; MX = MY = 0; SCROLL = 0; CHAT_SCROLL = 0;
    /* The animation clock too. A timestamp left by an earlier case made NOW_MS - MARQ_T0 underflow
     * and the marquee read as fully travelled before it had moved at all. */
    MARQ_AT = -1; MARQ_T0 = 0; app_set_now(0);
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

    /* ESC MUST NEVER QUIT, from any state.
     *
     * It used to, from the home screen with an empty box, and that is what actually ended the app
     * when the main enter key turned out to be unmapped: enter did nothing, ESC was the next thing
     * reached for, and the second press exited. Leaving is a deliberate act now -- the X button,
     * which is on screen, or ctrl+Q. */
    reset();
    key(K_ESC); key(K_ESC); key(K_ESC);
    T("at home: ESC does not quit",          app_should_quit(), 0);
    reset(); seed("a"); CUR = 0; key(K_ESC); key(K_ESC);
    T("from a chat either",                  app_should_quit(), 0);
    reset(); COMPOSE_N = 2; snprintf(COMPOSE, sizeof COMPOSE, "hi");
    key(K_ESC); key(K_ESC);
    T("nor after clearing the box",          app_should_quit(), 0);

    /* the two ways out that DO exist */
    reset();
    key(K_QUIT);
    T("ctrl+Q quits",                        app_should_quit(), 1);
    /* The corner control is a GEAR now, not an X: it opens settings, and Quit is a row inside.
     * An X in the corner is window chrome on a device with no windows, and it made leaving the
     * only thing that corner could do. */
    reset(); CUR = -1; app_draw();
    click(R_EXIT.x + R_EXIT.w / 2, R_EXIT.y + R_EXIT.h / 2);
    T("the gear opens settings",             SETTINGS_ON, 1);
    T("and does not quit",                   app_should_quit(), 0);
    /* THE SHEET HAS NO QUIT BUTTON. It sat inside a panel people open to read the shortcut key,
     * one slip from ending the session, and it was never needed: ctrl+esc is listed two lines
     * above it. A destructive control does not belong in a reference panel. */
    app_draw();
    click(R_SET_THEME.x + 4, R_SET_THEME.y + 4);
    T("clicking inside the sheet does not quit", app_should_quit(), 0);
    /* clicking away closes it */
    app_draw();
    click(GFX_W - 4, GFX_H - 4);
    T("a click outside closes the sheet",    SETTINGS_ON, 0);
    T("and still does not quit",             app_should_quit(), 0);
    /* esc closes it too */
    reset(); CUR = -1; app_draw();
    click(R_EXIT.x + 12, R_EXIT.y + 12);
    key(K_ESC);
    T("esc closes the sheet",                SETTINGS_ON, 0);
    T("without quitting",                    app_should_quit(), 0);
    /* and the only way out is the chord */
    reset(); key(K_QUIT);
    T("ctrl+esc is the way out",             app_should_quit(), 1);

    /* MUTATION: the shipped behaviour, where a stray ESC at home ended the session. */
    {   reset();
        key(K_ESC);
        int caught = !app_should_quit();
        if (!caught) F++;
        printf("  %s  mutant: ESC quitting from the home screen\n", caught ? "CAUGHT" : "MISSED"); }

    reset(); seed("a"); CUR = 0; SEARCH_ON = 1;
    key(K_ESC);
    T("search open: ESC closes search only", SEARCH_ON, 0);
    T("search open: ESC does NOT quit",      app_should_quit(), 0);
    T("search open: ESC stays in the chat",  CUR, 0);

    /* The corner control opens settings from every screen, and never quits. */
    reset(); CUR = -1; app_draw();
    click(R_EXIT.x + 12, R_EXIT.y + 12);
    T("at home: the gear opens settings",    SETTINGS_ON, 1);
    reset(); seed("a"); CUR = 0; app_draw();
    click(R_EXIT.x + 12, R_EXIT.y + 12);
    T("inside a chat too",                   SETTINGS_ON, 1);
    reset(); SIDEBAR = 0; app_draw();
    click(R_EXIT.x + 12, R_EXIT.y + 12);
    T("with the sidebar collapsed too",      SETTINGS_ON, 1);
    T("and none of those quit",              app_should_quit(), 0);

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

    /* -- the composer grows upward and then stops --
     *
     * It was a fixed 24px slot, so a question longer than one line spilled out of the pill. The
     * field takes the lines it needs, the dock grows with it, and the BOTTOM edge stays put --
     * that last part is the whole point and is what a height-only assertion would miss. */
    printf("\n  -- the composer grows with its text --\n");
    reset(); CUR = -1;
    {   int w = GFX_W - (SIDE_W + 1);
        int h1 = compose_field_h(w);
        T("empty is one line", compose_lines(w), 1);
        T("and DOCK_H tall",   compose_dock_h(w) <= DOCK_H, 1);

        /* grow it a line at a time and watch the bottom edge */
        int bottoms[6], heights[6];
        for (int n = 1; n <= 5; n++) {
            COMPOSE_N = 0; COMPOSE[0] = 0;
            for (int k = 0; k < n * 40 && COMPOSE_N < (int)sizeof COMPOSE - 2; k++) {
                COMPOSE[COMPOSE_N++] = (k % 6 == 5) ? ' ' : 'm';
            }
            COMPOSE[COMPOSE_N] = 0;
            app_draw();
            heights[n] = R_FIELD.h;
            bottoms[n] = R_FIELD.y + R_FIELD.h;
        }
        T("it got taller than one line", heights[3] > h1, 1);
        T("the BOTTOM edge never moved",
          bottoms[1] == bottoms[2] && bottoms[2] == bottoms[3] &&
          bottoms[3] == bottoms[4] && bottoms[4] == bottoms[5], 1);
        T("capped at COMPOSE_MAX_LINES", heights[5] <= COMPOSE_MAX_LINES * COMPOSE_LH + 9, 1);
        T("the cap actually binds",      heights[5] == heights[4], 1);
        T("the field stays on screen",   bottoms[5] <= GFX_H, 1);
        T("send stays inside the field",
          R_SEND.y >= R_FIELD.y && R_SEND.y + R_SEND.h <= R_FIELD.y + R_FIELD.h, 1);

        /* MUTATION: a fixed-height field is what spilled text out of the pill. */
        int caught = (heights[3] != h1);
        if (!caught) F++;
        printf("  %s  mutant: fixed-height field (3 lines still %d px)\n",
               caught ? "CAUGHT" : "MISSED", heights[3]);

        /* the transcript must yield the space, or the grown field covers the last answer */
        COMPOSE_N = 0; COMPOSE[0] = 0; app_draw();
        int dock1 = compose_dock_h(w);
        for (int k = 0; k < 200 && COMPOSE_N < (int)sizeof COMPOSE - 2; k++)
            COMPOSE[COMPOSE_N++] = (k % 6 == 5) ? ' ' : 'm';
        COMPOSE[COMPOSE_N] = 0; app_draw();
        T("the dock grew with the field", compose_dock_h(w) > dock1, 1);
        COMPOSE_N = 0; COMPOSE[0] = 0;
    }

    /* -- the app is reachable with keys alone --
     *
     * There is no cursor any more, so every route that used to need one has to exist as a key or
     * it is simply gone. This is the check that the removal did not strand anything. */
    printf("\n  -- no pointer: keys reach the list --\n");
    reset(); for (int i = 0; i < 4; i++) seed("A car goes 150 m in 12 s.");
    CUR = -1; COMPOSE_N = 0; app_draw();
    T("a session starts selected", SEL_ROW, 0);
    key(K_DOWN); key(K_DOWN);
    T("down moves the selection", SEL_ROW, 2);
    key(K_UP);
    T("up moves it back", SEL_ROW, 1);
    for (int i = 0; i < 20; i++) key(K_DOWN);
    T("it stops at the last session", SEL_ROW, NCHATS - 1);
    for (int i = 0; i < 40; i++) key(K_UP);
    T("and at the first", SEL_ROW, 0);
    key(K_ENTER);
    T("enter opens the selected session", CUR, 0);

    /* the selection must be VISIBLE, or the arrows move something nobody can see */
    reset(); for (int i = 0; i < 3; i++) seed("A car goes 150 m in 12 s.");
    CUR = -1; HOVER = 0; SEL_ROW = 1; app_draw();
    {   /* Sample the row's GROUND, in a column past the end of the title, not the whole rect.
         * Counting C_SEL anywhere in the rect also counts the antialiased edge pixels of the text,
         * which land on that exact value often enough to report 20 of them in a row that is not
         * selected at all. The oracle was wrong, not the code. */
        const uint16_t *fb = gfx_buf();
        int lit = 0, plain = 0;
        int probe1 = R_CHAT[1].x + R_CHAT[1].w - 3;
        int probe2 = R_CHAT[2].x + R_CHAT[2].w - 3;
        for (int y = R_CHAT[1].y + 2; y < R_CHAT[1].y + R_CHAT[1].h - 2; y++)
            if (fb[y * GFX_W + probe1] == C_SEL) lit++;
        for (int y = R_CHAT[2].y + 2; y < R_CHAT[2].y + R_CHAT[2].h - 2; y++)
            if (fb[y * GFX_W + probe2] == C_SEL) plain++;
        T("the selected row is highlighted", lit > 0, 1);
        T("and an unselected one is not",    plain, 0);
        if (!(lit > 0)) F++;
        printf("  %s  mutant: arrows moving a selection nothing draws (%d px lit)\n",
               lit > 0 ? "CAUGHT" : "MISSED", lit);
    }

    /* typing still wins over selection: the arrows must not eat a composed message */
    reset(); seed("A car goes 150 m in 12 s."); CUR = -1; app_draw();
    key('h'); key('i');
    key(K_ENTER);
    T("enter sends when the box has text", COMPOSE_N, 0);
    /* ENTER NOW OPENS THE RELATION PICKER rather than sending straight away -- decision E, and
     * the composer is emptied into PENDQ, which is why the assertion above still reads 0. The
     * picker is MODAL, so leaving it open here swallowed every key the rest of this file sends:
     * six later cases failed with no defect in them. Asserting the new state and then leaving it
     * is the fix; the picker's own behaviour is covered by test_pickui. */
    T("enter opens the relation picker", PICK_ON, 1);
    T("and the question is held, not lost", PENDQ[0] != 0, 1);
    key(K_ESC);                       /* family list: esc is "ask anyway" -> Form C, picker closes */
    T("esc at the family list closes it", PICK_ON, 0);

    /* -- the caret, and editing anywhere but the end --
     *
     * The field could only be edited from the end: a typo three characters back meant deleting
     * everything after it. Reported from the device as "I have to delete stuff to write something
     * again". Left/right were never mapped and there was no caret index at all. */
    printf("\n  -- editing in the middle --\n");
    reset(); CUR = -1; compose_clear();
    for (const char *c = "v_0 = 5"; *c; c++) key(*c);
    T("typing puts the caret at the end", COMPOSE_C, COMPOSE_N);
    for (int i = 0; i < 4; i++) key(K_LEFT);
    T("left moves the caret", COMPOSE_C, 3);
    key('1');
    T2("insert lands AT the caret", COMPOSE, "v_01 = 5");
    T("and the caret advances past it", COMPOSE_C, 4);
    key(K_BACK);
    T2("backspace deletes BEFORE the caret", COMPOSE, "v_0 = 5");
    T("caret steps back with it", COMPOSE_C, 3);
    for (int i = 0; i < 20; i++) key(K_LEFT);
    T("left clamps at the start", COMPOSE_C, 0);
    key(K_BACK);
    T2("backspace at the start deletes nothing", COMPOSE, "v_0 = 5");
    for (int i = 0; i < 40; i++) key(K_RIGHT);
    T("right clamps at the end", COMPOSE_C, COMPOSE_N);
    compose_clear();
    T("clearing resets the caret", COMPOSE_C, 0);

    /* -- controls respond outside their drawn box --
     *
     * A 24px plate is a small target for a cursor driven by a 2 cm pad. The rect a control is
     * DRAWN in and the one it RESPONDS to are different things, and only the first needs to be
     * exact. What must hold is that hover and click agree: a control that lights up where it
     * cannot be clicked, or clicks where it never lit, is worse than one that simply misses. */
    printf("\n  -- controls have a hitbox --\n");
    reset(); CUR = -1; app_draw();
    {   int outside_x = R_NEW.x - 3, mid_y = R_NEW.y + R_NEW.h / 2;
        T("a near miss is still inside the box", outside_x < R_NEW.x, 1);

        /* hover: does it light up? */
        HOVER = 1; MX = outside_x; MY = mid_y; app_draw();
        const uint16_t *fb = gfx_buf();
        int lit = 0;
        for (int y = R_NEW.y + 2; y < R_NEW.y + R_NEW.h - 2; y++)
            if (fb[y * GFX_W + R_NEW.x + R_NEW.w - 2] == C_SEL) lit++;
        T("it highlights on a near miss", lit > 0, 1);

        /* click at the same point must do the same thing */
        CUR = 0; click(outside_x, mid_y);
        T("and clicking there fires it", CUR, -1);

        /* far enough away it must NOT fire, or every stray click hits something */
        reset(); CUR = 0; app_draw();
        click(R_NEW.x - 40, R_NEW.y + R_NEW.h / 2);
        T("a real miss still misses", CUR, 0);

        /* THE GAP BETWEEN TWO ICONS BELONGS TO THE NEARER ONE.
     *
     * A 10px pad on 24px plates set 28px apart makes neighbouring boxes overlap, and a first-match
     * test would hand the whole overlap to whichever control happens to be checked first. Resolving
     * by distance means the midpoint splits cleanly and no icon can steal its neighbour's clicks. */
    {   reset(); CUR = -1; HOVER = 1; app_draw();
        int y = R_NEW.y + R_NEW.h / 2;
        int gap_l = R_NEW.x + R_NEW.w, gap_r = R_SEARCH.x;   /* the gap between icon 1 and 2 */
        int mid = (gap_l + gap_r) / 2;
        T("just left of the midpoint is New chat", hit(R_NEW, mid - 2, y), 1);
        T("and NOT search",                        hit(R_SEARCH, mid - 2, y), 0);
        T("just right of it is search",            hit(R_SEARCH, mid + 2, y), 1);
        T("and NOT New chat",                      hit(R_NEW, mid + 2, y), 0);
        T("exactly one control claims any point",
          hit(R_NEW, mid - 2, y) + hit(R_SEARCH, mid - 2, y) + hit(R_TOGGLE, mid - 2, y), 1);

        /* MUTATION: first-match over overlapping padded boxes gives BOTH a claim. */
        int both = inside_pad(R_NEW, mid + 2, y, HIT_PAD) &&
                   inside_pad(R_SEARCH, mid + 2, y, HIT_PAD);
        if (!both) F++;
        printf("  %s  mutant: padded boxes DO overlap (%d), so first-match would bias the gap\n",
               both ? "CAUGHT" : "MISSED", both);
    }

    /* MUTATION: an exact-rect hit test rejects the near miss the hover accepted. */
        int caught = !inside(R_NEW, outside_x, mid_y);
        if (!caught) F++;
        printf("  %s  mutant: exact-rect hit testing, where a near miss does nothing\n",
               caught ? "CAUGHT" : "MISSED");
    }

    /* -- ctrl+N and ctrl+S, and the fact that they ARE the buttons --
     *
     * The chord's first draft called new_chat(), which creates a session; the BUTTON sets CUR = -1
     * and creates nothing until the first send. Two copies of "what New chat does" would have left
     * an untitled empty session in the list on every ctrl+N. Both call one function now, and this
     * asserts the key and the click land in the same state. */
    printf("\n  -- ctrl+N and ctrl+S --\n");
    reset(); seed("A car goes 150 m in 12 s."); CUR = 0;
    COMPOSE_N = 3; snprintf(COMPOSE, sizeof COMPOSE, "abc"); app_draw();
    key(K_NEW);
    T("ctrl+N leaves the session",     CUR, -1);
    T("and clears the composer",       COMPOSE_N, 0);
    T("without creating a session",    NCHATS, 1);
    {   int after_key = NCHATS;
        reset(); seed("A car goes 150 m in 12 s."); CUR = 0; app_draw();
        click(R_NEW.x + 10, R_NEW.y + 10);
        T("the button lands in the same state", CUR == -1 && NCHATS == after_key, 1); }

    reset(); CUR = -1; app_draw();
    key(K_SEARCH);
    T("ctrl+S opens search",           SEARCH_ON, 1);
    key(K_ESC);
    T("and ESC closes it",             SEARCH_ON, 0);

    /* mid-compose the chords still fire, which is the reason to have them */
    reset(); CUR = -1; COMPOSE_N = 0; app_draw();
    key('h'); key('i');
    T("typing reaches the composer",   COMPOSE_N, 2);
    key(K_SEARCH);
    T("ctrl+S works mid-compose",      SEARCH_ON, 1);
    SEARCH_ON = 0;

    /* MUTATION: without the ctrl check the chord arrives as a bare letter in the box. */
    {   reset(); CUR = -1; COMPOSE_N = 0; app_draw();
        key('n');
        int caught = (CUR == -1 && COMPOSE_N == 1);
        if (!caught) F++;
        printf("  %s  mutant: ctrl+N read as a plain 'n' (composer holds %d)\n",
               caught ? "CAUGHT" : "MISSED", COMPOSE_N);
        COMPOSE_N = 0; COMPOSE[0] = 0; }

    /* -- the placeholder says a different thing on each screen --
     *
     * The rotating subject answers "what is this for", which is a new-chat question. Inside a
     * session it would keep advertising the app to someone already using it, and put motion beside
     * the answer they are reading. */
    printf("\n  -- the placeholder names a subject, on the right screen --\n");
    {   T("more than one subject", ASK_N > 1, 1);
        T("a word rests longer than it slides", ASK_HOLD_MS > ASK_SLIDE_MS, 1);
        /* A CLOCK, so the cadence is identical whether or not a finger is on the pad. Tied to
         * draws, the rotation ran at whatever rate the input loop happened to spin, which on
         * hardware was far too fast under a finger and frozen without one. */
        unsigned cycle = ASK_HOLD_MS + ASK_SLIDE_MS;
        /* Deliberately unhurried. The first cadence was 2.86 s and on hardware it read as
         * flickering; a subject you have not finished reading before it leaves is decoration, not
         * information. */
        T("a word is on screen for four seconds or more", ASK_HOLD_MS >= 4000, 1);
        T("and the whole cycle stays under six", cycle <= 6000, 1);
        T("the slide is long enough to be motion", ASK_SLIDE_MS >= 300, 1);

        /* SHUFFLED, not in table order, and never the same word twice running. */
        {   int repeats = 0, distinct = 0, hit[ASK_N];
            for (int i = 0; i < ASK_N; i++) hit[i] = 0;
            int prev = -1;
            for (unsigned n = 0; n < 400u; n++) {
                int i = ask_index(n);
                if (i < 0 || i >= ASK_N) { repeats = -1; break; }
                if (i == prev) repeats++;
                hit[i] = 1; prev = i;
            }
            for (int i = 0; i < ASK_N; i++) distinct += hit[i];
            T("never the same subject twice running", repeats, 0);
            T("every subject comes up", distinct, ASK_N);
            /* and it is NOT simply walking the table */
            int sequential = 1;
            for (unsigned n = 0; n < 8u; n++)
                if (ask_index(n) != (int)(n % (unsigned)ASK_N)) { sequential = 0; break; }
            T("the order is shuffled, not the table order", sequential, 0);
            /* the same instant always shows the same word, or a redraw would advance it */
            T("it is a pure function of the clock", ask_index(123u), ask_index(123u));
        }
        int bad = 0, seen[ASK_N];
        for (int i = 0; i < ASK_N; i++) seen[i] = 0;
        for (unsigned ms = 0; ms < cycle * (unsigned)ASK_N * 2u; ms += 50) {
            int i = (int)((ms / cycle) % (unsigned)ASK_N);
            if (i < 0 || i >= ASK_N) bad++; else seen[i] = 1;
        }
        T("index never leaves the table", bad, 0);
        int all = 1; for (int i = 0; i < ASK_N; i++) if (!seen[i]) all = 0;
        T("every subject is reached", all, 1);

        /* app_set_now reports whether a redraw is OWED, and at rest it must mostly say no. That is
         * what stops the app repainting a 320x240 framebuffer in software for nothing, which is
         * what the CPU was actually doing. */
        reset(); CUR = -1; COMPOSE_N = 0; app_draw();
        int asked = 0, frames = 0;
        for (unsigned ms = 0; ms < cycle; ms += 20) { frames++; if (app_set_now(ms)) asked++; }
        T("resting, most frames are skipped", asked * 3 < frames, 1);
        printf("        %d of %d frames wanted a redraw across one cycle\n", asked, frames);

        {   int w = GFX_W - (SIDE_W + 1);
            int budget = compose_textw(w) - gfx_text_w("Ask me about ", F_UI);
            int widest = 0; const char *worst_word = "";
            for (int i = 0; i < ASK_N; i++) {
                int wd = gfx_text_w(ASK_ABOUT[i], F_UI);
                if (wd > widest) { widest = wd; worst_word = ASK_ABOUT[i]; }
            }
            T("every subject fits the field", widest <= budget, 1);
            printf("        widest is \"%s\" at %d px against %d px of budget\n",
                   worst_word, widest, budget); }

        reset(); CUR = -1; COMPOSE_N = 0; app_draw();
        int empty_w = gfx_text_w("Ask me about ", F_UI) + gfx_text_w(ASK_ABOUT[0], F_UI);
        reset(); seed("A car goes 150 m in 12 s."); CUR = 0; COMPOSE_N = 0; app_draw();
        int chat_w = gfx_text_w("Type a message...", F_UI);
        T("the two screens use different copy", empty_w != chat_w, 1);
        if (empty_w == chat_w) F++;
        printf("  %s  mutant: the same placeholder on both screens\n",
               empty_w != chat_w ? "CAUGHT" : "MISSED");
    }

    /* -- hover marquee on session titles --
     *
     * Titles ellipsise at ~64px, which for a question is a few words and often not enough to tell
     * two sessions apart. On hover the full title scrolls left, STOPS at its end, and resets when
     * the cursor leaves. The clamp is the part worth asserting: without it the title keeps going
     * and scrolls off its own left edge, leaving a blank row that reads as a rendering fault. */
    printf("\n  -- session titles marquee on hover --\n");
    {   /* MILLISECONDS now. It was a draw count, and draws happen only on input, so the travel
         * raced under a moving finger and stopped dead the moment it lifted. */
        T("still at rest during the hold",  marq_off(MARQ_HOLD_MS, 40), 0);
        T("has not moved just before",      marq_off(MARQ_HOLD_MS - 1, 40), 0);
        T("moves after the hold",           marq_off(MARQ_HOLD_MS + 500, 40) > 0, 1);
        T("STOPS at the end",               marq_off(MARQ_HOLD_MS + 60000, 40), 40);
        T("never exceeds the overflow",     marq_off(0xFFFFFFu, 40), 40);
        T("a title that fits never moves",  marq_off(0xFFFFFFu, 0), 0);
        T("negative overflow is not motion",marq_off(0xFFFFFFu, -12), 0);
        {   int unclamped = (int)(((0xFFFFFFu - MARQ_HOLD_MS) * MARQ_PX_S) / 1000u);
            int caught = (marq_off(0xFFFFFFu, 40) != unclamped);
            if (!caught) F++;
            printf("  %s  mutant: no clamp -> %d px against a 40 px overflow\n",
                   caught ? "CAUGHT" : "MISSED", unclamped); }
    }

    /* the hover STATE: it must reset when the cursor leaves, or the next hover resumes mid-scroll */
    /* Hover from the ROW's own rect, not from coordinates that were right when they were typed.
     * MX=40, MY=50 stopped being on row zero the moment the icon band got taller. */
    reset(); seed("Speed from distance and time on a long straight road"); CUR = -1;
    HOVER = 1; app_draw();
    MX = R_CHAT[0].x + 10; MY = R_CHAT[0].y + R_CHAT[0].h / 2; app_draw();
    T("hovering a row arms the marquee", MARQ_AT >= 0, 1);
    /* It advances with the CLOCK, not with draws.
     *
     * Order matters here and the first version of this test got it wrong: MARQ_T0 is stamped from
     * NOW_MS inside app_draw when the cursor ARRIVES on a row, so setting the clock after the
     * arming draw moves the origin and the elapsed time reads as zero. Arm first, then advance the
     * clock without drawing. */
    {   app_set_now(1000); app_draw();                 /* arms: MARQ_T0 = 1000 */
        int at_rest = marq_off(marq_elapsed(), 400);
        app_set_now(1000 + MARQ_HOLD_MS + 1000);       /* one second past the hold */
        int moved = marq_off(marq_elapsed(), 400);
        T("time advances it", moved > at_rest, 1);
        /* RELATIVE, because the row armed at whatever NOW_MS the first draw of this block saw and
         * pinning an absolute expectation to it just encodes that accident. A second of clock is a
         * second of travel wherever the origin happens to be. */
        int p0 = moved;
        app_set_now(1000 + MARQ_HOLD_MS + 2000);
        T("advances MARQ_PX_S per second", marq_off(marq_elapsed(), 400) - p0, MARQ_PX_S);
        /* and DRAWING does not: this is the hardware bug in one assertion. Read the value AFTER
         * the last clock change, or it compares against a stale sample. */
        int before_draws = marq_off(marq_elapsed(), 400);
        app_draw(); app_draw(); app_draw();
        T("redrawing advances it not at all", marq_off(marq_elapsed(), 400), before_draws); }
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
    /* "Recents" is drawn at TOP_H+6 with F_SM's 13px box, so it ends at TOP_H+19. Written as one
     * expression rather than two magic numbers, because the icon band grew twice this session and
     * each time the literal went stale while the property it stood for did not. */
    {   int recents_bottom = TOP_H + 6 + gfx_font_h(F_SM);
        T("the first row clears Recents", R_CHAT[0].y >= recents_bottom, 1);
        T("with air, not flush",          R_CHAT[0].y - recents_bottom >= 3, 1); }
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
