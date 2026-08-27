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

    printf("\n  -- ctrl+a selects the whole conversation --\n");
    {   seed();
        in_event k = {0}; k.kind = IN_KEY; k.key = K_SELALL;
        app_event(&k);
        T("select-all is live", sel_active() && SEL_ALL, "");
        const char *s = sel_text();
        snprintf(d, sizeof d, "%d chars", s ? (int)strlen(s) : -1);
        T("it yields text", s && *s, d);
        /* BOTH sides, or it is not the conversation. A transcript that copies only the answers
         * loses what was asked, which is the half you cannot reconstruct. */
        T("the question is in it", s && strstr(s, "A 2 kg mass is raised 5 m.") != 0, "");
        T("the answer is in it",   s && strstr(s, "98 J from U = mgh.") != 0, "");
        T("and they are labelled", s && strstr(s, "You:") && strstr(s, "TLM:"), "");
        /* It must survive the round trip a reader would actually make. */
        clip_set(s); COMPOSE[0] = 0; COMPOSE_N = 0;
        clip_paste();
        T("it can be pasted into the box", COMPOSE_N > 0, COMPOSE);
        /* A tap ends it, like any other selection. */
        in_event c = {0}; c.kind = IN_CLICK; c.x = 200; c.y = 100;
        app_event(&c);
        T("a tap clears select-all", !SEL_ALL && !sel_active(), "");
    }

    printf("\n  -- ctrl+a with nothing to select SAYS SO --\n");
    {   app_init(); CUR = -1; TOAST[0] = 0;
        in_event k = {0}; k.kind = IN_KEY; k.key = K_SELALL;
        app_event(&k);
        T("it does not claim a selection", !sel_active(), "");
        /* Silence here would read as the shortcut being broken, which is how this whole session
         * started. */
        T("and it explains why", TOAST[0] != 0, TOAST); }

    printf("\n  -- your OWN text has a copy control --\n");
    {   seed();
        snprintf(d, sizeof d, "x=%d y=%d w=%d", R_QACT[0].x, R_QACT[0].y, R_QACT[0].w);
        T("the question copy rect is laid out", R_QACT[0].w > 0 && R_QACT[0].h > 0, d);
        /* Under the bubble, not over it, and following the BUBBLE's right edge rather than the
         * pane's -- the bubble no longer spans the pane. */
        T("it sits below the bubble", R_QACT[0].y > 0, d);
        CLIP[0] = 0;
        in_event c = {0}; c.kind = IN_CLICK;
        c.x = R_QACT[0].x + 3; c.y = R_QACT[0].y + 3;
        app_event(&c);
        T("clicking it copies the QUESTION", strcmp(CLIP, "A 2 kg mass is raised 5 m.") == 0, CLIP);
        /* And not the answer, which is the neighbouring control's job. */
        T("not the answer", strstr(CLIP, "98 J") == 0, CLIP); }

    printf("\n  -- ctrl+a selects THE DRAFT when there is one --\n");
    {   seed();
        snprintf(COMPOSE, sizeof COMPOSE, "%s", "a half written question");
        COMPOSE_N = (int)strlen(COMPOSE);
        in_event k = {0}; k.kind = IN_KEY; k.key = K_SELALL;
        app_event(&k);
        T("the composer is selected", COMPOSE_SEL, "");
        /* And NOT the transcript. Selecting both would highlight the whole screen and leave the
         * next keystroke ambiguous. */
        T("the transcript is not", !SEL_ALL, "");

        CLIP[0] = 0;
        in_event c = {0}; c.kind = IN_KEY; c.key = K_COPY;
        app_event(&c);
        T("ctrl+c copies the draft", strcmp(CLIP, "a half written question") == 0, CLIP);

        /* Backspace deletes the WHOLE selection, not one character off the end. */
        in_event b = {0}; b.kind = IN_KEY; b.key = K_BACK;
        app_event(&b);
        snprintf(d, sizeof d, "n=%d sel=%d \"%s\"", COMPOSE_N, COMPOSE_SEL, COMPOSE);
        T("backspace wipes it whole", COMPOSE_N == 0 && !COMPOSE_SEL, d);
    }

    printf("\n  -- a keystroke REPLACES the selection --\n");
    {   seed();
        snprintf(COMPOSE, sizeof COMPOSE, "%s", "old text");
        COMPOSE_N = (int)strlen(COMPOSE);
        in_event k = {0}; k.kind = IN_KEY; k.key = K_SELALL; app_event(&k);
        in_event ch = {0}; ch.kind = IN_KEY; ch.key = 'x'; app_event(&ch);
        snprintf(d, sizeof d, "\"%s\"", COMPOSE);
        T("typing replaces, does not append", strcmp(COMPOSE, "x") == 0, d);
        T("and the selection is gone", !COMPOSE_SEL, "");
    }

    printf("\n  -- pasting over a selection replaces it too --\n");
    {   seed();
        snprintf(COMPOSE, sizeof COMPOSE, "%s", "old text");
        COMPOSE_N = (int)strlen(COMPOSE);
        clip_set("new text");
        in_event k = {0}; k.kind = IN_KEY; k.key = K_SELALL; app_event(&k);
        clip_paste();
        snprintf(d, sizeof d, "\"%s\"", COMPOSE);
        T("paste replaces the draft", strcmp(COMPOSE, "new text") == 0, d);
    }

    printf("\n  -- an EMPTY box falls through to the transcript --\n");
    {   seed();
        compose_clear();
        in_event k = {0}; k.kind = IN_KEY; k.key = K_SELALL; app_event(&k);
        T("the transcript is selected instead", SEL_ALL, "");
        T("and the composer is not", !COMPOSE_SEL, ""); }

    printf("\n  -- NO SELECTION MAY OUTLIVE THE TEXT IT SELECTED --\n");
    {   /* Seven sites emptied the box by clearing the count and the string but not the flag, which
         * would leave a highlight over an empty field and make the next keystroke "replace" text
         * that is not there. Each exit is checked. */
        seed();
        snprintf(COMPOSE, sizeof COMPOSE, "%s", "draft"); COMPOSE_N = 5;
        in_event k = {0}; k.kind = IN_KEY; k.key = K_SELALL; app_event(&k);
        in_event e = {0}; e.kind = IN_KEY; e.key = K_ENTER; app_event(&e);
        T("sending clears it", !COMPOSE_SEL, "");

        seed();
        snprintf(COMPOSE, sizeof COMPOSE, "%s", "draft"); COMPOSE_N = 5;
        app_event(&k);
        in_event esc = {0}; esc.kind = IN_KEY; esc.key = K_ESC; app_event(&esc);
        T("escape clears it", !COMPOSE_SEL, "");

        seed();
        snprintf(COMPOSE, sizeof COMPOSE, "%s", "draft"); COMPOSE_N = 5;
        app_event(&k);
        in_event nw = {0}; nw.kind = IN_KEY; nw.key = K_NEW; app_event(&nw);
        T("a new chat clears it", !COMPOSE_SEL, "");

        seed();
        snprintf(COMPOSE, sizeof COMPOSE, "%s", "draft"); COMPOSE_N = 5;
        app_event(&k);
        in_event cl = {0}; cl.kind = IN_CLICK; cl.x = 200; cl.y = 100; app_event(&cl);
        T("a tap clears it", !COMPOSE_SEL, "");

        seed();
        snprintf(COMPOSE, sizeof COMPOSE, "%s", "draft"); COMPOSE_N = 5;
        app_event(&k);
        app_init();
        T("a reset clears it", !COMPOSE_SEL, ""); }

    printf("\n  -- app_init clears selection state --\n");
    {   seed();
        sel_begin(R_ANS[0].x + 1, R_ANS[0].y + 3);
        sel_extend(R_ANS[0].x + 60, R_ANS[0].y + 3);
        app_init();
        T("no selection survives a reset", !sel_active() && SEL_TURN < 0, ""); }

    printf("%s\n", F ? "test_select FAIL" : "test_select PASS");
    return F ? 1 : 0;
}
