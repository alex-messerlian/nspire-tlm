/* Drag-selection, the clipboard, and the answer actions.
 *
 * These are the three features a reader can operate with the pointer and nothing else, and every
 * one of them fails SILENTLY when it fails: a selection that does not take looks like a selection
 * of nothing, a copy that copies nothing looks like a copy, and an action row that never draws
 * looks like an app without one. The first render of the highlight showed no highlight at all and
 * the pixels could not say whether the probe had missed, the range was empty, or the colour was
 * too faint -- which is why the state is asserted here rather than eyeballed.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"

static int F;
static void T(const char *n, int ok, const char *d) {
    if (!ok) F++;
    printf("  %s  %-48s %s\n", ok ? "PASS" : "FAIL", n, d ? d : "");
}

static const char *ANS = "The gravitational potential energy is 98 J from U = mgh.";

static void seed(void) {
    app_init();
    app_chat *c = &CHATS[0];
    memset(c, 0, sizeof *c);
    snprintf(c->title, sizeof c->title, "%s", "A 2 kg mass raised 5 m");
    snprintf(c->turn[0].q, sizeof c->turn[0].q, "%s", "A 2 kg mass is raised 5 m.");
    snprintf(c->turn[0].a, sizeof c->turn[0].a, "%s", ANS);
    c->nturns = 1; c->used = 1; c->turn[0].done = 1;
    NCHATS = 1; CUR = 0; SCROLL = 0;
    HOVER = 1; MX = 300; MY = 220;
    app_draw();                      /* fills R_ANS and R_ACT */
}

int main(void) {
    char d[160];
    gfx_init();
    printf("test_select\n");

    printf("\n  -- the answer's drawn block is recorded, or nothing below can work --\n");
    seed();
    snprintf(d, sizeof d, "x=%d y=%d w=%d h=%d", R_ANS[0].x, R_ANS[0].y, R_ANS[0].w, R_ANS[0].h);
    T("R_ANS[0] has a real rect", R_ANS[0].w > 0 && R_ANS[0].h > 0, d);

    printf("\n  -- a drag selects, and selects what it dragged over --\n");
    {   int x0 = R_ANS[0].x, y0 = R_ANS[0].y + 3;
        sel_begin(x0 + 1, y0);
        int a0 = SEL_A;
        sel_extend(x0 + 60, y0);
        snprintf(d, sizeof d, "a=%d b=%d anchor=%d", SEL_A, SEL_B, SEL_ANCHOR);
        T("dragging right makes a non-empty range", sel_active(), d);
        T("the range starts where the press did", SEL_A == a0, d);
        const char *s = sel_text();
        snprintf(d, sizeof d, "\"%s\"", s ? s : "(null)");
        T("sel_text returns the dragged text", s && *s, d);
        /* The selected text must be a real substring of the answer, not an offset misread as one.
         * An off-by-one in the probe would still produce a plausible-looking string. */
        T("and it is a substring of the answer", s && strstr(ANS, s) != 0, d);
    }

    printf("\n  -- dragging LEFT selects too; the anchor is not the minimum --\n");
    {   int x0 = R_ANS[0].x, y0 = R_ANS[0].y + 3;
        sel_begin(x0 + 60, y0);
        sel_extend(x0 + 1, y0);
        snprintf(d, sizeof d, "a=%d b=%d", SEL_A, SEL_B);
        T("backwards drag is still a range", sel_active() && SEL_A < SEL_B, d);
    }

    printf("\n  -- NEGATIVE ARMS: what must NOT select --\n");
    {   sel_begin(2, 2);                       /* the sidebar, nowhere near an answer */
        T("a press off the transcript selects nothing", !sel_active(), "");
        seed();
        sel_begin(R_ANS[0].x + 1, R_ANS[0].y + 3);
        sel_extend(R_ANS[0].x + 1, R_ANS[0].y + 3);   /* no movement */
        T("a press with no drag selects nothing", !sel_active(), "");
    }

    printf("\n  -- the clipboard --\n");
    {   seed();
        CLIP[0] = 0; COMPOSE[0] = 0; COMPOSE_N = 0;
        T("copying the answer fills the clipboard", clip_set(CHATS[0].turn[0].a) && CLIP[0], CLIP);
        clip_paste();
        snprintf(d, sizeof d, "compose=\"%s\"", COMPOSE);
        T("paste puts it in the message box", strcmp(COMPOSE, ANS) == 0, d);
        /* Pasting an empty clipboard must say so rather than appear to work. */
        CLIP[0] = 0; COMPOSE[0] = 0; COMPOSE_N = 0; TOAST[0] = 0;
        clip_paste();
        T("pasting nothing leaves the box empty", COMPOSE_N == 0, COMPOSE);
        T("and it says so", TOAST[0] != 0, TOAST);
        /* Truncation must be reported. The composer is smaller than the clipboard by design. */
        COMPOSE[0] = 0; COMPOSE_N = 0; TOAST[0] = 0;
        memset(CLIP, 'x', sizeof CLIP - 1); CLIP[sizeof CLIP - 1] = 0;
        clip_paste();
        snprintf(d, sizeof d, "n=%d cap=%d toast=\"%s\"", COMPOSE_N, (int)sizeof COMPOSE - 1, TOAST);
        T("an oversized paste is trimmed, not refused", COMPOSE_N == (int)sizeof COMPOSE - 1, d);
        T("and the trim is reported", strstr(TOAST, "trim") != 0, TOAST);
    }

    printf("\n  -- the action row exists and is reachable --\n");
    {   seed();
        snprintf(d, sizeof d, "copy x=%d y=%d w=%d", R_ACT[0][0].x, R_ACT[0][0].y, R_ACT[0][0].w);
        T("three action rects are laid out", R_ACT[0][0].w > 0 && R_ACT[0][1].w > 0 && R_ACT[0][2].w > 0, d);
        /* They must not overlap, or a press on one lands on its neighbour -- and the neighbour of
         * thumbs-up is thumbs-DOWN. */
        T("copy does not overlap thumb-up",
          R_ACT[0][0].x + R_ACT[0][0].w <= R_ACT[0][1].x, d);
        T("thumb-up does not overlap thumb-down",
          R_ACT[0][1].x + R_ACT[0][1].w <= R_ACT[0][2].x, d);
        /* Reserved space means the row sits INSIDE the transcript, not under the composer. */
        T("the row is above the dock", R_ACT[0][0].y + R_ACT[0][0].h < GFX_H - DOCK_H, d);
    }

    printf("\n  -- a rating is recorded, and pressing again clears it --\n");
    {   seed();
        in_event e = {0}; e.kind = IN_CLICK;
        e.x = R_ACT[0][1].x + 3; e.y = R_ACT[0][1].y + 3;
        app_event(&e);
        T("thumbs up sets +1", CHATS[0].turn[0].rating == 1, "");
        app_event(&e);
        T("pressing it again clears it", CHATS[0].turn[0].rating == 0, "");
        e.x = R_ACT[0][2].x + 3; e.y = R_ACT[0][2].y + 3;
        app_event(&e);
        T("thumbs down sets -1", CHATS[0].turn[0].rating == -1, "");
    }

    printf("\n  -- a tap clears a selection; a drag release does not --\n");
    {   seed();
        sel_begin(R_ANS[0].x + 1, R_ANS[0].y + 3);
        sel_extend(R_ANS[0].x + 60, R_ANS[0].y + 3);
        T("selection is live before the tap", sel_active(), "");
        in_event e = {0}; e.kind = IN_CLICK; e.x = 200; e.y = 100;
        app_event(&e);
        T("a tap clears it", !sel_active(), "");
        /* The release of a DRAG arrives as a move with pressed=0, and must leave it standing. */
        seed();
        in_event m = {0}; m.kind = IN_MOVE; m.hover = 1; m.pressed = 1;
        m.x = R_ANS[0].x + 1; m.y = R_ANS[0].y + 3; app_event(&m);
        m.x = R_ANS[0].x + 60;                      app_event(&m);
        T("a press-drag builds a selection", sel_active(), "");
        m.pressed = 0;                              app_event(&m);
        T("releasing the drag keeps it", sel_active(), "");
    }

    printf("\n  -- app_init clears selection state --\n");
    {   seed();
        sel_begin(R_ANS[0].x + 1, R_ANS[0].y + 3);
        sel_extend(R_ANS[0].x + 60, R_ANS[0].y + 3);
        app_init();
        T("no selection survives a reset", !sel_active() && SEL_TURN < 0, ""); }

    printf("%s\n", F ? "test_select FAIL" : "test_select PASS");
    return F ? 1 : 0;
}
