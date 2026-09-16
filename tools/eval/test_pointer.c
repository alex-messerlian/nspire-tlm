/* Touchpad gestures, driven as sample sequences.
 *
 * WHY IT EXISTS. The device test reported "the mouse is a little broken, it's like tap to click and
 * not click to click". Pressing the pad down did nothing; only a light tap worked. The cause was
 * one branch in `pointer_poll`, and NOTHING IN THE REPO COULD HAVE CAUGHT IT: that function lived
 * inside device_app.c, which pulls libndls and only cross-compiles, so the only way to exercise it
 * was to push a .tns and use the calculator. That is the same position `app_request` was in when it
 * shipped with `int idx = 0` and showed the model Hooke's law for every question ever asked.
 *
 * So the state machine was lifted into src/store/pointer.c, which knows nothing about libndls, the
 * screen or the app, and this drives it with the exact gesture the user performed.
 *
 * THE GESTURE THAT WAS BROKEN, spelled out, because the sample order is the whole bug:
 *
 *   contact=1 pressed=0   finger lands            -> anchor
 *   contact=1 pressed=1   pad pushed down         -> press edge
 *   contact=0 pressed=1   finger LIFTS while still pushing, which is one physical motion
 *
 * The old code took the was_pressed branch on that last sample, which deliberately emits no click
 * because it was written for a press-DRAG where a click would clear the selection just made. True
 * of a drag. Not true of a click, and a click is the common case.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/pointer.c"

static int F;
#define OK(cond, ...) do { int _c = !!(cond); if (!_c) F++; \
    printf("  %s  ", _c ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); } while (0)

#define PAD_W 400
#define PAD_H 300
#define SCR_W 320
#define SCR_H 240

/* Feed a sample; record what came out. */
typedef struct { int clicks, moves, last_x, last_y, last_pressed; } tally;

static void feed(ns_pointer *p, tally *t, int contact, int pressed, int x, int y) {
    ns_pad_sample s = { .contact = contact, .pressed = pressed, .x = x, .y = y };
    in_event e;
    memset(&e, 0, sizeof e);
    if (!ns_pointer_feed(p, &s, &e)) return;
    if (e.kind == IN_CLICK) t->clicks++;
    if (e.kind == IN_MOVE)  t->moves++;
    t->last_x = e.x; t->last_y = e.y; t->last_pressed = e.pressed;
}

static void start(ns_pointer *p, tally *t) {
    ns_pointer_init(p, PAD_W, PAD_H, SCR_W, SCR_H);
    memset(t, 0, sizeof *t);
}

int main(void) {
    ns_pointer p; tally t;

    /* ---- 1. THE REPORTED BUG: press the pad down and lift in one motion ---------------- */
    start(&p, &t);
    feed(&p, &t, 1, 0, 200, 150);        /* finger lands           */
    feed(&p, &t, 1, 1, 200, 150);        /* pad pushed down        */
    feed(&p, &t, 0, 1, 200, 150);        /* lifts WHILE pressed    */
    OK(t.clicks == 1, "a physical click that lifts while pressed emits one click (got %d)", t.clicks);

    /* ---- 2. press, release, THEN lift: also one click ---------------------------------- */
    start(&p, &t);
    feed(&p, &t, 1, 0, 200, 150);
    feed(&p, &t, 1, 1, 200, 150);
    feed(&p, &t, 1, 0, 200, 150);        /* released, finger still down */
    feed(&p, &t, 0, 0, 200, 150);
    OK(t.clicks == 1, "press then release then lift emits exactly one click (got %d)", t.clicks);

    /* ---- 3. a light tap, which always worked and must keep working --------------------- */
    start(&p, &t);
    feed(&p, &t, 1, 0, 200, 150);
    feed(&p, &t, 0, 0, 200, 150);
    OK(t.clicks == 1, "a tap with no press emits one click (got %d)", t.clicks);

    /* ---- 4. a PRESS-DRAG must still emit NO click -------------------------------------- */
    start(&p, &t);
    feed(&p, &t, 1, 0, 100, 150);
    feed(&p, &t, 1, 1, 100, 150);
    for (int i = 1; i <= 12; i++) feed(&p, &t, 1, 1, 100 + i * 15, 150);   /* drag right  */
    feed(&p, &t, 0, 1, 280, 150);
    OK(t.clicks == 0, "a press-drag emits NO click, so it cannot clear its own selection (got %d)",
       t.clicks);
    OK(t.moves > 0, "a press-drag does emit movement (%d)", t.moves);

    /* ---- 5. a swipe with no press must emit no click ----------------------------------- */
    start(&p, &t);
    feed(&p, &t, 1, 0, 100, 150);
    for (int i = 1; i <= 12; i++) feed(&p, &t, 1, 0, 100 + i * 15, 150);
    feed(&p, &t, 0, 0, 280, 150);
    OK(t.clicks == 0, "a swipe is not a click (got %d)", t.clicks);

    /* ---- 6. a tap does not move the cursor --------------------------------------------- */
    start(&p, &t);
    int x0 = p.cx, y0 = p.cy;
    /* SIZED AGAINST THE ACCUMULATOR, not guessed. The first version jittered by 2 pad units, which
     * cannot move the cursor even with the dead zone removed (accel(2) = 160, and 160 * 320/400 is
     * 128, below the 256 the accumulator needs), so the case proved nothing and its control
     * survived. Two samples of 3 pad units total 6, still inside MOVE_DEADZONE = 7, and would move
     * the cursor by a pixel without it. */
    feed(&p, &t, 1, 0, 200, 150);
    feed(&p, &t, 1, 0, 203, 150);
    feed(&p, &t, 1, 0, 206, 150);
    feed(&p, &t, 0, 0, 206, 150);
    OK(p.cx == x0 && p.cy == y0, "jitter under the dead zone does not move the cursor (%d,%d)",
       p.cx, p.cy);
    OK(t.clicks == 1, "...and it is still a click (got %d)", t.clicks);

    /* ---- 7. a press-drag that barely moves is a CLICK, not a swallowed drag ------------ */
    start(&p, &t);
    feed(&p, &t, 1, 0, 200, 150);
    feed(&p, &t, 1, 1, 200, 150);
    feed(&p, &t, 1, 1, 201, 150);        /* under both the dead zone and TAP_SLOP */
    feed(&p, &t, 0, 1, 201, 150);
    OK(t.clicks == 1, "a press that hardly moves is a click, not a drag (got %d)", t.clicks);

    /* ---- 8. the cursor stays on screen ------------------------------------------------- */
    start(&p, &t);
    feed(&p, &t, 1, 0, 10, 10);
    for (int i = 0; i < 60; i++) feed(&p, &t, 1, 0, 10 + i * 40, 10);
    OK(p.cx >= 0 && p.cx < SCR_W, "cursor clamped in x (%d)", p.cx);
    start(&p, &t);
    feed(&p, &t, 1, 0, 200, 290);
    for (int i = 0; i < 60; i++) feed(&p, &t, 1, 0, 200, 290 - i * 40);
    OK(p.cy >= 0 && p.cy < SCR_H, "cursor clamped in y (%d)", p.cy);

    /* ---- 9. a click reports hover=0, so a hover state is not left stuck on -------------- */
    start(&p, &t);
    feed(&p, &t, 1, 0, 200, 150);
    feed(&p, &t, 1, 1, 200, 150);
    feed(&p, &t, 0, 1, 200, 150);
    OK(t.last_pressed == 0, "the click event reports pressed=0 (got %d)", t.last_pressed);

    /* ---- 10. two clicks in a row both arrive (state resets) ---------------------------- */
    start(&p, &t);
    for (int k = 0; k < 2; k++) {
        feed(&p, &t, 1, 0, 200, 150);
        feed(&p, &t, 1, 1, 200, 150);
        feed(&p, &t, 0, 1, 200, 150);
    }
    OK(t.clicks == 2, "two consecutive physical clicks both arrive (got %d)", t.clicks);

    /* ---- 11. no contact at all produces nothing ---------------------------------------- */
    start(&p, &t);
    for (int i = 0; i < 5; i++) feed(&p, &t, 0, 0, 0, 0);
    OK(t.clicks == 0 && t.moves == 0, "an idle pad emits nothing (%d clicks, %d moves)",
       t.clicks, t.moves);

    printf("%s test_pointer: %d failure%s\n", F ? "FAIL" : "PASS", F, F == 1 ? "" : "s");
    return F ? 1 : 0;
}
