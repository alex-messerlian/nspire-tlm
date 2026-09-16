#ifndef NS_POINTER_H
#define NS_POINTER_H
/* The touchpad gesture state machine, lifted out of device_app.c so it can be tested on the host.
 *
 * WHY IT WAS LIFTED. A device test reported "the mouse is a little broken, it's like tap to click
 * and not click to click": pressing the pad down did nothing, only a light tap worked. The cause
 * was one branch, and it was unreachable by any check in the repo because `pointer_poll` lived
 * inside device_app.c, which pulls libndls and only cross-compiles. The project log's rule for exactly
 * this: when device-only code needs verifying, the question is not how to simulate it but what the
 * smallest pure unit is. This is that unit -- it takes touchpad samples and emits in_events, and
 * knows nothing about libndls, the screen or the app.
 *
 * device_app.c keeps touchpad_scan() and passes what it reads straight in.
 */
#include "app.h"

typedef struct {
    int contact;        /* a finger is on the pad                     */
    int pressed;        /* that finger is pushing the pad down        */
    int x, y;           /* pad coordinates, y bottom-up               */
} ns_pad_sample;

typedef struct {
    /* geometry, set once from touchpad_getinfo() */
    int pad_w, pad_h, scr_w, scr_h;
    /* cursor */
    int cx, cy;
    /* gesture state */
    int down, travel, have_ref, ref_x, ref_y, acc_x, acc_y, pad_travel, moving, was_pressed;
} ns_pointer;

void ns_pointer_init(ns_pointer *p, int pad_w, int pad_h, int scr_w, int scr_h);

/* Feed one sample. Returns 1 if `e` was filled with an event to dispatch, 0 if nothing happened. */
int ns_pointer_feed(ns_pointer *p, const ns_pad_sample *s, in_event *e);

#endif
