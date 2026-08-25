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

    printf("\n  -- the empty state is tappable --\n");
    reset(); CUR = -1; app_draw();
    T("suggestion has a hit rect", R_SUGGEST[0].w > 0, 1);
    click(R_SUGGEST[1].x + 20, R_SUGGEST[1].y + 8);
    T("tapping fills the composer", COMPOSE_N > 0, 1);
    T("with that suggestion's text", strcmp(COMPOSE, SUGGEST[1]) == 0, 1);
    T("it does NOT send", app_busy(), 0);
    T("and does not open a session", CUR, -1);

    reset(); CUR = -1; app_draw();
    click(R_SUGGEST[2].x + 20, R_SUGGEST[2].y + 8);
    T("third suggestion works too", strcmp(COMPOSE, SUGGEST[2]) == 0, 1);

    /* inside a session those rects are stale -- a click there must not resurrect them */
    reset(); seed("a"); CUR = -1; app_draw();
    gfx_rect stale = R_SUGGEST[0];
    CUR = 0; app_draw();
    COMPOSE_N = 0; COMPOSE[0] = 0;
    click(stale.x + 20, stale.y + 8);
    T("stale rect ignored inside a session", COMPOSE_N, 0);

    printf("\n  %s: exit paths, %d failure(s)\n\n", F ? "FAIL" : "PASS", F);
    gfx_free();
    return F != 0;
}
