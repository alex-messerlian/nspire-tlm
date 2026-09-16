/* Touchpad gestures. See pointer.h for why this is a separate translation unit.
 *
 * Moved verbatim out of device_app.c except for the defect named below, so the comments are the
 * ones written while tuning it against the physical pad.
 */
#include "pointer.h"

#define TAP_SLOP  10          /* cursor px of travel still counted as a tap */
/* A TAP MUST NOT MOVE THE POINTER, which is why clicking took two goes.
 *
 * The second sample after touch-down moved the cursor, so the finger jitter of a tap dragged the
 * pointer a few pixels off whatever it was aimed at and the click landed elsewhere. Tapping again
 * appeared to work only because the first tap had already shifted the cursor onto the target.
 *
 * Nothing moves until the finger has travelled past this much, in PAD units. Below it the touch is
 * a tap; above it, it is a swipe and stays one for the rest of the contact. */
#define MOVE_DEADZONE 7

void ns_pointer_init(ns_pointer *p, int pad_w, int pad_h, int scr_w, int scr_h) {
    for (unsigned i = 0; i < sizeof *p; i++) ((char *)p)[i] = 0;
    p->pad_w = pad_w > 0 ? pad_w : 1;
    p->pad_h = pad_h > 0 ? pad_h : 1;
    p->scr_w = scr_w; p->scr_h = scr_h;
    p->cx = scr_w / 2; p->cy = scr_h / 2;
}

static int accel(int d) {
    int a = d < 0 ? -d : d;
    /* thresholds in PAD units per sample; tuned so a deliberate swipe crosses the screen */
    /* HALVED, roughly. The first curve topped out at 3.5x and a flick overshot the whole screen,
     * so the cursor arrived somewhere past wherever you were aiming. These reach 2x, which still
     * crosses most of the panel in one stroke while leaving the top end controllable. */
    /* Eased down again, about a fifth, to smooth the top end. The pointer was reported as good at
     * the previous curve and just slightly quick. */
    int mul = a < 4 ? 80             /* 0.31x: fine placement                */
            : a < 10 ? 152           /* 0.59x                                 */
            : a < 20 ? 256           /* 1x                                    */
                     : 400;          /* 1.56x                                 */
    return d * mul;
}

int ns_pointer_feed(ns_pointer *p, const ns_pad_sample *s, in_event *e) {
    if (!s->contact) {                        /* lift: drop the anchor, finish a tap or a click */
        p->have_ref = 0;
        if (p->was_pressed) {
            /* A PHYSICAL CLICK ENDS HERE TOO, AND IT USED TO BE SWALLOWED.
             *
             * This branch existed for the press-DRAG -- press, move, release -- where reporting a
             * click would clear the selection the drag just made. That reasoning is right for a
             * drag and wrong for a click, and a click is what a user does far more often: press
             * the pad down and lift, both in one motion, so the finger leaves while `pressed` is
             * still set and the release edge below never runs.
             *
             * The result on the device was that ONLY a light tap worked, reported back as "it's
             * like tap to click and not click to click".
             *
             * The two cases are separated by the same test a tap already uses: if the cursor did
             * not travel, nothing was dragged and there is no selection to protect, so it is a
             * click. If it did travel, it stays a drag and still emits no click.
             */
            int moved = p->travel > TAP_SLOP;
            p->was_pressed = 0; p->down = 0; p->travel = 0;
            if (!moved) {
                e->kind = IN_CLICK; e->x = p->cx; e->y = p->cy;
                e->hover = 0; e->pressed = 0;
                return 1;
            }
            e->kind = IN_MOVE; e->x = p->cx; e->y = p->cy; e->hover = 1; e->pressed = 0;
            return 1;
        }
        if (p->down) {
            p->down = 0;
            if (p->travel <= TAP_SLOP) {
                e->kind = IN_CLICK; e->x = p->cx; e->y = p->cy;
                e->hover = 0; e->pressed = 0;
                return 1;
            }
        }
        return 0;
    }

    /* The press EDGE is reported even with no movement, so the app sees where a drag started.
     * Everything below only fires when the cursor actually moves, which would swallow a press. */
    if (s->pressed != p->was_pressed) {
        /* ONE CLICK PATH, NOT TWO. A release with the finger still on the pad deliberately does
         * NOT emit a click here: `down` is still set, so the lift below emits exactly one. A
         * click here as well was a second route to the same event, which is the two-implementation
         * hazard this file was extracted to remove. Its negative control survived, which is how it
         * was noticed: the suite could not tell the two paths apart because only one ever fires. */
        p->was_pressed = s->pressed;
        e->kind = IN_MOVE; e->x = p->cx; e->y = p->cy; e->hover = 1; e->pressed = s->pressed;
        return 1;
    }

    if (!p->have_ref) {                        /* first sample: anchor, do NOT move */
        p->have_ref = 1; p->down = 1; p->travel = 0; p->pad_travel = 0; p->moving = 0;
        p->ref_x = s->x; p->ref_y = s->y; p->acc_x = p->acc_y = 0;
        return 0;
    }

    int dx = s->x - p->ref_x, dy = p->ref_y - s->y;   /* pad y is bottom-up */
    p->ref_x = s->x; p->ref_y = s->y;

    /* Below the dead zone this is still a tap. The reference keeps tracking, so when it does turn
     * into a swipe the cursor carries on from where it is rather than jumping by the slack. */
    if (!p->moving) {
        p->pad_travel += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        if (p->pad_travel <= MOVE_DEADZONE) return 0;
        p->moving = 1;
    }

    p->acc_x += accel(dx) * p->scr_w / p->pad_w;
    p->acc_y += accel(dy) * p->scr_h / p->pad_h;
    int mx = p->acc_x / 256, my = p->acc_y / 256;
    p->acc_x -= mx * 256; p->acc_y -= my * 256;      /* keep the remainder */
    if (!mx && !my) return 0;

    p->travel += (mx < 0 ? -mx : mx) + (my < 0 ? -my : my);
    p->cx += mx; p->cy += my;
    if (p->cx < 0) p->cx = 0;
    if (p->cx >= p->scr_w) p->cx = p->scr_w - 1;
    if (p->cy < 0) p->cy = 0;
    if (p->cy >= p->scr_h) p->cy = p->scr_h - 1;

    e->kind = IN_MOVE; e->x = p->cx; e->y = p->cy; e->hover = 1; e->pressed = s->pressed;
    return 1;
}
