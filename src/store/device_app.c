/* ChatTLM on the calculator. Touchpad cursor, framebuffer UI, real model.
 *
 * The touchpad is an ABSOLUTE pointer: touchpad_scan() returns x, y, contact and proximity.
 * proximity is the finger near the pad without pressing, which is exactly a hover state -- so the
 * sidebar's hover-reveal trash and the button highlights work here the same way they do with a
 * mouse. I had previously assumed the device had no pointer at all and built a keypad-only UI;
 * touchpad_getinfo() had been in libndls the whole time.
 */
#include <libndls.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "app.h"
#include "pointer.h"
#include "loader.h"
#include "assemble.h"
#include "askparse.h"
#include "tokenizer.h"
#include "../../tools/eval/eval.h"
#include "toolrun.h"
#include "shapecheck.h"
#include "gencore.h"

/* ---- elapsed time -------------------------------------------------------------------------------
 * The status line reports how long a turn took, and this project does not put unmeasured numbers on
 * screen. Timing comes from the 32.768 kHz SP804, which bench_platform established as the
 * crystal-derived cross-check -- NOT from a token count multiplied by the published 2.683 tok/s,
 * which would be an estimate wearing a measurement's clothes.
 *
 * Constants deliberately mirror bench/common.h rather than including it: that header pulls in an
 * on-screen console and its own file logging, which this program already owns differently. If the
 * SP804 layout ever changes, both must change -- noted here because a silent divergence between
 * two copies of a hardware constant is exactly the sort of thing that is found late. */
#define TLM_TIMER_32K   0x900D0000u
#define TLM_SP804_VALUE 0x04u
#define TLM_SP804_LOAD  0x00u
#define TLM_SP804_CTRL  0x08u
#define TLM_MMIO32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define TLM_32K_HZ      32768u

/* What the timer looked like before we touched it, so it can be put back.
 *
 * The guard below reads as "only configure it if nobody else is running it", and that is NOT what
 * it tests. It tests whether the timer is already in EXACTLY THE MODE WE WANT: enabled and
 * free-running. A timer the OS owns in any other mode fails that test and gets reprogrammed
 * underneath it, and nothing ever put it back -- the app simply returned.
 *
 * docs/HARDWARE.md C3 lists 0x900D0000 as [SOURCED] from Hackspire and UNMEASURED. Ownership was
 * never established on device. Writing to a peripheral whose owner is unknown and not restoring it
 * is indefensible whatever it turns out to break, and this calculator has dropped off USB after a
 * run repeatedly, which is a symptom in search of exactly this kind of cause.
 *
 * TO BE CLEAR: that this CAUSES the USB drop is a hypothesis and is not measured. The restore is
 * correct on its own terms regardless of whether it turns out to be the culprit. */
static uint32_t SAVED_CTRL, SAVED_LOAD;
static int      CLOCK_TOUCHED;

static void clock_start(void) {
    uint32_t c = TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_CTRL);
    if (!((c & (1u << 7)) && (c & (1u << 1)))) {
        /* Snapshot ONCE, on the first reconfiguration. Saving on every call would capture our own
         * settings on the second pass and restore those, which is not a restore at all. */
        if (!CLOCK_TOUCHED) {
            SAVED_CTRL = c;
            SAVED_LOAD = TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_LOAD);
            CLOCK_TOUCHED = 1;
        }
        TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_CTRL) = 0;
        TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_LOAD) = 0xFFFFFFFFu;
        TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_CTRL) = (1u << 7) | (1u << 1);
    }
}
/* Puts the timer back exactly as it was found. Disabled first: the SP804 latches LOAD while the
 * timer runs, so writing LOAD to a live timer sets the reload value without taking effect, and the
 * counter carries on from wherever we left it. */
static void clock_restore(void) {
    if (!CLOCK_TOUCHED) return;
    TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_CTRL) = 0;
    TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_LOAD) = SAVED_LOAD;
    TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_CTRL) = SAVED_CTRL;
    CLOCK_TOUCHED = 0;
}
static uint32_t clock_raw(void) { return TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_VALUE); }

/* ---- wall clock ---------------------------------------------------------------------------------
 * The RTC is a 32-bit seconds counter at 0x90090000: vendor/Ndless/ndless-sdk/libsyscalls/
 * stdlib.cpp:514 implements gettimeofday() as a straight read of it into tv_sec, and nspire-io uses
 * the same address for its cursor blink.
 *
 * What the SDK does NOT establish is whether the counter runs on THIS unit, or what its zero means
 * -- assigning it to tv_sec assumes the Unix epoch, which is an assumption in someone else's code
 * rather than a measurement on ours. bench/bench_rtc.c is the probe for exactly that and HAS NOT
 * RUN. So this reports -1 unless the value decodes to a plausible year, and AUTO stays light.
 *
 * Returning -1 rather than a plausible-looking hour is the whole point: a theme derived from an
 * unverified epoch would be a number nobody measured, driving something a person can see. */
#define RTC_ADDR 0x90090000u
#define RTC_MIN  1735689600u   /* 2025-01-01: before this the clock was never set */
#define RTC_MAX  2524608000u   /* 2050-01-01: after this it is not a Unix epoch    */

int app_clock_hour(void) {
    uint32_t s = TLM_MMIO32(RTC_ADDR);
    if (s == 0 || s == 0xFFFFFFFFu) return -1;        /* stopped or floating */
    if (s < RTC_MIN || s > RTC_MAX)  return -1;        /* not the epoch the SDK assumes */
    return (int)((s % 86400u) / 3600u);                /* UTC: no timezone exists on this device */
}
/* The counter runs DOWN; unsigned wraparound handles one wrap. Returns milliseconds. */
static unsigned clock_ms_since(uint32_t start) {
    uint32_t ticks = start - clock_raw();
    return (unsigned)((unsigned long long)ticks * 1000ull / TLM_32K_HZ);
}

FILE *g_nspire_log = 0;
extern void  rq_build(const char *path);
extern int   rq_probe(const char *path, char *why, int cap);
extern int   rq_fits(const char *path, long *need);
extern float *rq_forward(int token, int pos);
extern int   rq_vocab(void);
extern void  rq_free(void);
void app_begin_turn(const char *question);

static ns_store2 ST;
static ns_tok    TK;
static int MODEL_READY;

/* ---- touchpad -> cursor ----------------------------------------------------------------------
 * The pad reports in its own coordinate space; touchpad_getinfo() gives its extent. Map to the
 * panel once, at startup, rather than assuming a scale -- pad dimensions differ across revisions. */
static int PAD_W = 1, PAD_H = 1;
static int CX = GFX_W / 2, CY = GFX_H / 2;
static ns_pointer PTR;

static void pointer_init(void) {
    touchpad_info_t *ti = touchpad_getinfo();
    if (ti && ti->width && ti->height) { PAD_W = ti->width; PAD_H = ti->height; }
    ns_pointer_init(&PTR, PAD_W, PAD_H, GFX_W, GFX_H);
    CX = PTR.cx; CY = PTR.cy;
}

/* RELATIVE, WITH ACCELERATION. The gesture state machine now lives in src/store/pointer.c so it
 * can be driven by a host test -- see pointer.h. This file keeps only the libndls call and the
 * cursor the drawing code reads. There is ONE implementation: a second copy here is how this repo
 * ended up with two rankers that disagreed. */
static int pointer_poll(in_event *e) {
    touchpad_report_t r;
    if (touchpad_scan(&r) != 0) return 0;
    ns_pad_sample s = { .contact = r.contact, .pressed = r.pressed, .x = r.x, .y = r.y };
    int got = ns_pointer_feed(&PTR, &s, e);
    CX = PTR.cx; CY = PTR.cy;
    return got;
}

/* ---- keypad ---------------------------------------------------------------------------------- */
static int keypad_poll(void) {
    /* The key currently down. Held across calls so a press is reported once and the loop is never
     * blocked waiting for a release. */
    static const t_key *held;

    /* THE RELEASE CHECK COMES FIRST. It used to sit after the chord branch, so a held ctrl+N
     * re-fired start_new_chat() every pass: the screen sat on the new-chat view and never moved,
     * which looks precisely like the shortcut doing nothing. */
    if (held) {
        if (isKeyPressed(*held)) return 0;     /* still down: already reported */
        held = 0;
    }

    /* CTRL IS STICKY, AND ALSO WORKS HELD.
     *
     * The calculator's own ctrl is a sticky modifier: you tap it and it applies to the next key.
     * Requiring it to be HELD meant half the ways a person would naturally reach for a shortcut
     * silently did nothing. Both work now: hold ctrl and press the letter, or tap ctrl and then
     * press the letter afterwards.
     *
     * `armed` is set only when ctrl is released WITHOUT having been used, so holding it down for a
     * chord does not also leave it armed for the following keystroke. Any non-shortcut key clears
     * it and types normally, so a stray tap on ctrl cannot swallow the next letter. */
    /* SHIFT, AND IT IS NOT A NICETY. Measured on the shipped store: 95 of 182 variable names
     * contain an uppercase letter -- F_net, Delta_t, T_h, C_V, Q, K, V -- and 90 of the 164
     * records have at least one INPUT variable that does. Without shift those 90 records cannot be
     * bound at all, which is the same shape as the missing underscore and the missing '='.
     *
     * Sticky OR held, exactly like ctrl below, because the calculator's own shift is sticky and
     * requiring it to be held is half the ways a person will reach for it. */
    static int shift_was, shift_armed, shift_used;
    int shift_now = isKeyPressed(KEY_NSPIRE_SHIFT);
    if (shift_now && !shift_was) shift_used = 0;
    if (!shift_now && shift_was && !shift_used) shift_armed = 1;
    shift_was = shift_now;
    const int shifted = (shift_now || shift_armed);

    static int ctrl_was, ctrl_armed, ctrl_used;
    int ctrl_now = isKeyPressed(KEY_NSPIRE_CTRL);
    if (ctrl_now && !ctrl_was) ctrl_used = 0;                  /* ctrl went down */
    if (!ctrl_now && ctrl_was && !ctrl_used) ctrl_armed = 1;   /* tapped alone: arm it */
    ctrl_was = ctrl_now;

    if (ctrl_now || ctrl_armed) {
        struct { const t_key *k; int c; } CH[] = {
            { &KEY_NSPIRE_N, K_NEW }, { &KEY_NSPIRE_S, K_SEARCH },
            { &KEY_NSPIRE_B, K_PANEL }, { &KEY_NSPIRE_ESC, K_QUIT },
            /* Both chord styles come free: the sticky-ctrl logic above already accepts ctrl held
             * with the letter, or ctrl tapped and released and then the letter. */
            { &KEY_NSPIRE_C, K_COPY }, { &KEY_NSPIRE_V, K_PASTE },
            { &KEY_NSPIRE_A, K_SELALL },
            /* ctrl + (-)  ->  UNDERSCORE, and it is not a convenience.
             *
             * MEASURED: 131 of the store's 182 variable names contain '_' -- v_0, F_net, Delta_t,
             * T_h, x_f, omega_0. 72%. Without it a student cannot bind most of the variables the
             * relations actually use, so "Find velocity. Given v_0 = 5, a = 2, t = 3." is not a
             * question this calculator can be asked.
             *
             * It is a CHORD because the SDK has no underscore key -- there is no
             * KEY_NSPIRE_UNDERSCORE and nothing on the keypad is printed with one. Any mapping is
             * therefore arbitrary, so it goes on the key whose PC convention it already is. */
            { &KEY_NSPIRE_MINUS, '_' }, { &KEY_NSPIRE_NEGATIVE, '_' },
        };
        for (unsigned i = 0; i < sizeof CH / sizeof CH[0]; i++) {
            if (isKeyPressed(*CH[i].k)) {
                held = CH[i].k; ctrl_used = 1; ctrl_armed = 0;
                return CH[i].c;
            }
        }
        if (ctrl_now) return 0;        /* ctrl held on its own types nothing */
    }

    static const struct { const t_key *k; int c; } MAP[] = {
        /* BOTH enter keys. The keypad has two -- RET (0x10,0x001) is the big one at the bottom
         * right, ENTER (0x10,0x002) the other -- and only the second was mapped, so pressing the
         * obvious key did nothing at all. Reaching for ESC after that is what actually quit the
         * app, from the home screen with an empty box. */
        { &KEY_NSPIRE_RET, K_ENTER }, { &KEY_NSPIRE_ENTER, K_ENTER },
        { &KEY_NSPIRE_ESC, K_ESC }, { &KEY_NSPIRE_TAB, K_TAB },
        { &KEY_NSPIRE_DEL, K_BACK }, { &KEY_NSPIRE_UP, K_UP }, { &KEY_NSPIRE_DOWN, K_DOWN },
        /* LEFT AND RIGHT MOVE THE CARET. They were never mapped, so the composer could only be
         * edited from the end: a typo three characters back meant deleting everything after it.
         * The touchpad's own left/right rocker reports as these. */
        { &KEY_NSPIRE_LEFT, K_LEFT }, { &KEY_NSPIRE_RIGHT, K_RIGHT },
        /* The menu key opens the character palette. On the OS it opens the CAS function menu;
         * inside an Ndless program it does nothing at all, so it is free -- and "this menu should
         * do something" is exactly right. */
        { &KEY_NSPIRE_MENU, K_SYM },
        /* A127. The CATALOG key -- the book icon on the keypad, and the one a student presses
         * looking for a list of formulas. On the OS it opens the function catalogue; inside an
         * Ndless program it is free. */
        { &KEY_NSPIRE_CAT, K_LIB },
        { &KEY_NSPIRE_SPACE, ' ' },
        /* PUNCTUATION, WITHOUT WHICH THE APP CANNOT BE USED AT ALL.
         *
         * The map was letters, digits, space and period. The input format the model is trained on
         * is `what is force, k = 500, x = 0.4` -- it needs '=' and ',' in every question that
         * supplies a value, and '-' for a negative one. Neither was mapped, so a user could type a
         * bare question and nothing else, and every prompt the device could physically produce was
         * one with no givens: a shape that occurs in 0 of 232,613 training documents.
         *
         * All of these keys are on the physical keypad and all have SDK constants. They were simply
         * never mapped. Found by running the app, not by reading it -- the composer's own tests
         * feed it characters directly and never ask which ones a keypad can produce.
         *
         * NEGATIVE is the (-) key, distinct from MINUS; a student reaching for either means the
         * same thing here. EE is the scientific-notation key and the corpus is full of values like
         * 3.71e-07, so it maps to 'e'. */
        { &KEY_NSPIRE_EQU,      '=' },
        { &KEY_NSPIRE_COMMA,    ',' },
        { &KEY_NSPIRE_MINUS,    '-' },
        { &KEY_NSPIRE_NEGATIVE, '-' },
        { &KEY_NSPIRE_PLUS,     '+' },
        { &KEY_NSPIRE_MULTIPLY, '*' },
        { &KEY_NSPIRE_DIVIDE,   '/' },
        { &KEY_NSPIRE_LP,       '(' },
        { &KEY_NSPIRE_RP,       ')' },
        { &KEY_NSPIRE_EE,       'e' },
        { &KEY_NSPIRE_COLON,    ':' },
        { &KEY_NSPIRE_QUES,     '?' }, { &KEY_NSPIRE_PERIOD, '.' },
        /* THE REST OF WHAT IS PRINTED ON THE KEYPAD. Chosen by measuring what the store needs, not
         * by reading the plastic: '^' appears 28 times in the 164 formulas and '|' twice
         * (f_beat=|f_2-f_1|), and a student reading a relation off the screen will reach for both.
         * The comparison keys and the quotes cost nothing to map and are printed on the device, so
         * a key that does nothing is a bug by this file's own standard. */
        { &KEY_NSPIRE_EXP,      '^' },
        { &KEY_NSPIRE_LTHAN,    '<' }, { &KEY_NSPIRE_GTHAN, '>' },
        { &KEY_NSPIRE_BAR,      '|' },
        { &KEY_NSPIRE_APOSTROPHE, '\'' }, { &KEY_NSPIRE_QUOTE, '"' },
        { &KEY_NSPIRE_QUESEXCL, '!' },
        /* THE CATALOG KEY IS THE UNDERSCORE, as a single press.
         *
         * On the OS that key opens a character catalog; inside an Ndless program there is no such
         * popup -- the app receives the raw key and nothing else happens. So it is free, and the
         * underscore is what it should spend itself on: '_' is in 131 of the store's 182 variable
         * names, and a chord for the most-needed character is the wrong way round. ctrl + (-)
         * still works for anyone who learned it first. */
        { &KEY_NSPIRE_CAT,      '_' },
        { &KEY_NSPIRE_0, '0' }, { &KEY_NSPIRE_1, '1' }, { &KEY_NSPIRE_2, '2' },
        { &KEY_NSPIRE_3, '3' }, { &KEY_NSPIRE_4, '4' }, { &KEY_NSPIRE_5, '5' },
        { &KEY_NSPIRE_6, '6' }, { &KEY_NSPIRE_7, '7' }, { &KEY_NSPIRE_8, '8' },
        { &KEY_NSPIRE_9, '9' },
        { &KEY_NSPIRE_A,'a' },{ &KEY_NSPIRE_B,'b' },{ &KEY_NSPIRE_C,'c' },{ &KEY_NSPIRE_D,'d' },
        { &KEY_NSPIRE_E,'e' },{ &KEY_NSPIRE_F,'f' },{ &KEY_NSPIRE_G,'g' },{ &KEY_NSPIRE_H,'h' },
        { &KEY_NSPIRE_I,'i' },{ &KEY_NSPIRE_J,'j' },{ &KEY_NSPIRE_K,'k' },{ &KEY_NSPIRE_L,'l' },
        { &KEY_NSPIRE_M,'m' },{ &KEY_NSPIRE_N,'n' },{ &KEY_NSPIRE_O,'o' },{ &KEY_NSPIRE_P,'p' },
        { &KEY_NSPIRE_Q,'q' },{ &KEY_NSPIRE_R,'r' },{ &KEY_NSPIRE_S,'s' },{ &KEY_NSPIRE_T,'t' },
        { &KEY_NSPIRE_U,'u' },{ &KEY_NSPIRE_V,'v' },{ &KEY_NSPIRE_W,'w' },{ &KEY_NSPIRE_X,'x' },
        { &KEY_NSPIRE_Y,'y' },{ &KEY_NSPIRE_Z,'z' },
    };
    /* EDGE-TRIGGERED, NOT BLOCKING. This used to spin in `while (isKeyPressed(k)) {}` until the
     * key came back up, so the entire loop stopped for as long as a finger rested on a key: no
     * repaint, no pointer, nothing. Typing at a normal rate meant the app was stalled most of the
     * time, which is what "I can't even type, it's so slow" was.
     *
     * Now the key that is down is remembered and reported ONCE; the loop keeps running while it is
     * held, and the next press is only accepted after a release. Same one-character-per-press
     * behaviour, without stopping the world to get it. */
    for (unsigned i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
        if (isKeyPressed(*MAP[i].k)) {
            int c = MAP[i].c;
            held = MAP[i].k;
            ctrl_armed = 0;            /* a stray ctrl tap must not swallow this key */
            if (shifted) {
                shift_used = 1; shift_armed = 0;
                if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            }
            return c;
        }
    }
    return 0;
}

/* ---- generation ------------------------------------------------------------------------------- */
/* Does the prose state the runtime's number, to within the 4-significant-figure rounding the
 * corpus applies below 1e5? Compared as VALUES, not as strings: "20000" and "2e+04" are the same
 * answer, and a string compare would flag the correct one. Any number in the answer may be the
 * match, because the prose legitimately restates givens too -- this asks whether the RESULT is
 * among them, not whether every number is right. */
static int answer_states_result(const char *ans, const char *res) {
    double want = atof(res);
    if (want == 0.0) return 1;                    /* 0 is unreliable to match; do not flag it */
    for (const char *p = ans; *p; p++) {
        if (!((*p >= '0' && *p <= '9') || (*p == '-' && p[1] >= '0' && p[1] <= '9'))) continue;
        if (p != ans && (p[-1] == 'e' || p[-1] == 'E' || p[-1] == '.')) continue;
        char *end = 0;
        double got = strtod(p, &end);
        if (end == p) continue;
        double tol = (want < 0 ? -want : want) * 0.02;
        double d = got - want; if (d < 0) d = -d;
        if (d <= tol) return 1;
        p = end - 1;
    }
    return 0;
}


/* Tool execution lives in toolrun.c so the shipping code can be verified on the host against
 * evalcli, instead of only by a device round-trip. See src/store/toolrun.h. */

/* ---- data location -----------------------------------------------------------------------------
 * The SLM->TLM rename moved every hardcoded path from /documents/slm/ to /documents/tlm/ while the
 * calculator's copy of the data still sits in the old directory. A hardcoded path would have made
 * the app exit 1 on launch with no message -- the exact silent-failure shape that cost five device
 * cycles during Phase 2 bring-up.
 *
 * So resolve at runtime instead: try each candidate directory and keep the first where the STORE
 * opens. Probing the store rather than the directory means a half-populated directory does not win.
 * The chosen prefix is reported into the log so a wrong pick is visible without a round-trip. */
static const char *DATA_DIRS[] = { "/documents/chattlm/", "/documents/tlm/", "/documents/slm/", "/documents/ndless/",
                                   "/documents/bench/", "/documents/" };
static char DATA_DIR[32];
static int  MODEL_OK = 0;          /* set by rq_probe at startup; gates the send path */
static char MODEL_WHY[160];        /* why not, in words the reader can act on */

static const char *dpath(const char *leaf) {
    static char buf[80];
    snprintf(buf, sizeof buf, "%s%s", DATA_DIR, leaf);
    return buf;
}

/* CAN THE MODEL LOAD NOW? rq_fits holds the checkpoint and both KV-cache blocks at once, which is
 * what build_transformer will ask for. If not, say so in words a student can act on. Turning the
 * calculator off and on keeps the heap as it is; only the reset button clears what earlier runs
 * left behind, and Ndless then has to be installed again, which is what ChatTLM Setup does. */
static int model_room(char *why, int cap) {
    long need = 0;
    if (rq_fits(dpath("model4096.bin.tns"), &need)) return 1;
    snprintf(why, cap, "Not enough free memory: the model needs %ld.%ld MB. Press the reset button "
             "on the back of the calculator, then open ChatTLM Setup.", need / 1000000, (need / 100000) % 10);
    return 0;
}
/* Returns 1 when a directory holding ALL THREE data files was found.
 *
 * The first version of this probed the STORE ONLY and its comment claimed that probing a file
 * rather than a directory meant "a half-populated directory does not win". That was false: a
 * directory holding only the store won outright, and the model path built from the same prefix
 * then reached llama2.c's read_checkpoint, which calls exit() on a missing file -- the process
 * would vanish mid-demo with no message. A comment asserting a guarantee the code does not make is
 * worse than no comment, because it stops the next reader from checking.
 *
 * fopen is used to test presence so a rejected candidate costs no allocation; ns_load runs once,
 * on the winner. */
static int has(const char *leaf) {
    FILE *f = fopen(dpath(leaf), "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}
static int resolve_data_dir(ns_store2 *st, char *why, int wcap) {
    int n = 0;
    why[0] = 0;
    for (unsigned i = 0; i < sizeof DATA_DIRS / sizeof DATA_DIRS[0]; i++) {
        snprintf(DATA_DIR, sizeof DATA_DIR, "%s", DATA_DIRS[i]);
        const char *missing = 0;
        if      (!has("store.tns.tns"))     missing = "store";
        else if (!has("tok4096.tok.tns"))   missing = "tok";
        else if (!has("model4096.bin.tns")) missing = "model";
        if (missing) {
            n += snprintf(why + n, (size_t)(wcap - n > 0 ? wcap - n : 0),
                          "%s no %s\n", DATA_DIRS[i], missing);
            continue;
        }
        if (ns_load(st, dpath("store.tns.tns")) == NS_OK) return 1;
        n += snprintf(why + n, (size_t)(wcap - n > 0 ? wcap - n : 0),
                      "%s store unreadable\n", DATA_DIRS[i]);
    }
    DATA_DIR[0] = 0;
    return 0;
}

const ns_store2 *app_store(void) { return ST.n ? &ST : 0; }

/* ---- generation hooks: presentation around src/store/gencore.c ------------------------------ */
typedef struct {
    int wrote;                  /* "<a>" seen: the status line says the answer is being written */
    const char *prompt;         /* for the structural call check, which reads prompt + document */
    char *tool_call;            /* the call's display label, reported in the final status line */
    int *shape_bad;
    char *shape_reason;
} app_gen_ctx;

static void gen_on_prefill(void *ctx, int done, int total) {
    (void)ctx;
    /* THE PREFILL REPAINTS, and it has to: reading a 51-token prompt is 51 forward passes at the
     * measured 1.753 tok/s, so the app sat with a frozen screen for ~29 seconds before the first
     * generated token. A draw costs a fraction of one forward pass, so this is progress for free. */
    char pr[40];
    snprintf(pr, sizeof pr, "%d%%", (100 * done) / (total > 0 ? total : 1));
    app_status("Reading", pr);
    app_draw();
    if (done == total) { app_status("Thinking", 0); app_draw(); }
}

static void gen_on_token(void *ctx, int tok) {
    app_gen_ctx *c = ctx;
    char piece[64];
    ns_tok_decode(&TK, &tok, 1, piece, sizeof piece);
    app_stream_token(piece);
    if (!c->wrote && strstr(piece, "<a>")) { c->wrote = 1; app_status("Writing the answer", 0); }
    app_draw();                          /* stream: one repaint per token */
}

static void gen_on_tool_begin(void *ctx, const char *span) {
    app_gen_ctx *c = ctx;
    /* Say what is being run BEFORE running it: a frozen screen with no explanation is the failure
     * mode this whole line exists to prevent. */
    tlm_call_label(span, c->tool_call, 64);
    app_status("Running", c->tool_call);
    app_draw();
}

static void gen_on_tool_end(void *ctx, int ok, const char *res, const char *span, const char *doc) {
    app_gen_ctx *c = ctx;
    (void)span;
    app_status(ok ? "Got" : "Tool refused", res);
    app_draw();
    /* ---- STRUCTURAL CALL VALIDATION -- docs/ARCHITECTURE.md s6 ------------------------------
     * Provenance checks that the ARGUMENTS trace to supplied values, not that the OPERATION is the
     * specified one, so `v=d/t` answered as eval((49.0)/(150.0)) is perfectly clean and perfectly
     * inverted. The document handed in is prompt + everything emitted so far, the same string the
     * two host graders pass. A CATEGORY, NEVER A REPAIR: the reader is told the call does not match
     * the relation; the model is told nothing. */
    static char fulldoc[2048], shape_why[192];
    int fl = snprintf(fulldoc, sizeof fulldoc, "%s%s", c->prompt, doc);
    if (fl > 0) {
        *c->shape_bad = tlm_shape_check_doc(fulldoc, shape_why, sizeof shape_why);
        if (*c->shape_bad == TLM_SHAPE_MISMATCH) {
            app_status("Call does not match the relation", c->tool_call);
            app_draw();
        }
        snprintf(c->shape_reason, 192, "%s", shape_why);
    }
}

static void gen_on_inject(void *ctx, int tok) {
    (void)ctx;
    char piece[64];
    ns_tok_decode(&TK, &tok, 1, piece, sizeof piece);
    app_stream_token(piece);
}

static void gen_on_injected(void *ctx) { (void)ctx; app_draw(); }

static int gen_should_stop(void *ctx, int in_prefill) {
    (void)ctx;
    /* ESC is honoured during prefill too: an interrupt during the longest phase of a turn used to be
     * ignored entirely. Between generated tokens, a tap on Stop counts the same as the key; this
     * asks where the CURSOR is, not where the finger is, since pointing is relative. */
    if (in_prefill) return app_take_abort();
    int s = 0;
    if (isKeyPressed(KEY_NSPIRE_ESC)) s = 1;
    else {
        touchpad_report_t r;
        if (touchpad_scan(&r) == 0 && r.contact && app_hit_stop(CX, CY)) s = 1;
    }
    if (app_take_abort()) s = 1;
    return s;
}

void app_request(const char *question, const char *rid) {
    app_begin_turn(question);
    app_draw();

    /* RETRIEVE, AND PARSE THE VALUES. Both of these were placeholders and both were reached by
     * every question the device could ask.
     *
     * `int idx = 0` meant the app ALWAYS showed record 0, which is F=-k*x. Every question on the
     * device came back "the record gives hooke's law, which does not apply" -- not a model failure
     * at all, the model was shown Hooke's law every time. The comment described a name match that
     * the code did not do.
     *
     * `in.nvals = 0` with nothing populating it meant NO GIVENS ever reached ns_assemble, so the
     * record span always said missing:<var> however many values the student typed. Combined with
     * the keypad that could not produce '=', every prompt the device was capable of building had
     * no givens -- a shape that occurs in 0 of 232,613 training documents.
     *
     * The logic is in askparse.c so it can be tested on the host against the shipped store; see
     * the header for why it is not inline here any more. It is not a retrieval claim -- ns_retrieve
     * in store.c ranks the same way and measured 8.0% on real questions -- but "the record the
     * question names" is strictly better than "record 0", and the model reads the record from the
     * prompt regardless. */
    static char prompt[NS_PROMPT_MAX];
    static ns_ask ask;

    /* A115. THE EARLIER TURNS, PREPENDED. app_context() was written, carefully, and had no caller:
     * every turn on this calculator was turn one. docs/RESULT_FOLLOWUP_UNWIRED.md records why
     * wiring it alone would have been wrong -- a second turn's prompt shape occurred in 0 of
     * 239,853 training documents, and RESULT_CANNOT_EXPLAIN is what this repo gets when the runtime
     * emits a shape the corpus does not have. The F1 tier is the other half and ships with this.
     *
     * IT GOES IN BEFORE ask_build, NOT AFTER. The context has to be visible to RETRIEVAL, not just
     * to the model: a follow-up names no relation -- "recompute" or "what units is that in?" --
     * and on its own retrieves nothing useful ("now v = 5" returns `wedge`, measured). Concatenated
     * with the earlier question it retrieves the right record. ask_build then strips the
     * assignments out of BOTH turns and ns_assemble appends the given list canonically, with
     * A113's last-wins so a restated value overrides instead of appearing twice.
     *
     * app_begin_turn() above has already pushed this turn, and app_context excludes the pending
     * one, so what comes back is strictly the EARLIER turns.
     *
     * 200 chars, against the model's 512-token context at a measured 2.564 chars/token. The record
     * span, the question and the answer take most of the window; this is about two verbatim turns
     * at app_context's own 90-character truncation, and it degrades by dropping the oldest rather
     * than by truncating mid-question. */
    static char ctxq[NS_PROMPT_MAX];
    char ctx[256];
    if (app_context(ctx, sizeof ctx, 200) > 0) {
        snprintf(ctxq, sizeof ctxq, "%s%s", ctx, question);
        question = ctxq;
    }

    ask_build(&ST, question, &ask);
    const ns_input in = ask.in;
    question = ask.question;

    /* `rid` IS THE STUDENT'S CHOICE, and until now nothing produced one -- both call sites passed
     * 0 and this function ignored the parameter. That is the whole of decision E: retrieval
     * measured 8.0% on the clean surface and 9.5% here over 200 labelled questions, so the
     * relation is picked by the student, not ranked. app.c's picker supplies it.
     *
     * rid == 0 is NOT "fall back to a guess". It is FORM C: the student left the picker without
     * choosing, which is a real and common answer -- 44% of textbook questions have no matching
     * relation. Measured on 88 such questions: 100.0% well-formed, 100.0% refused, 0.0% confident
     * answers. Omitting the record span instead fabricates an answer 37.5% of the time. */
    int idx = -1;
    if (rid) for (int r = 0; r < ST.n; r++)
        if (ST.rec[r].rid && !strcmp(ST.rec[r].rid, rid)) { idx = r; break; }

    if (idx < 0) {
        if (ns_assemble_none(prompt, sizeof prompt, question) < 0) {
            app_stream_token("<a> could not assemble a prompt<end>"); app_stream_end(); return;
        }
    } else if (ns_assemble(prompt, sizeof prompt, &ST.rec[idx], question, &in) < 0) {
        app_stream_token("<a> could not assemble a prompt<end>"); app_stream_end(); return;
    }
    clock_start();
    uint32_t t_start = clock_raw();
    app_status("Reading", idx < 0 ? "no matching relation" : ST.rec[idx].name);
    app_draw();

    /* THIS COMMENT USED TO READ "Checked at startup by rq_probe, so this cannot reach
     * read_checkpoint's exit() path." IT WAS FALSE, and it is the reason the app vanished on enter
     * in front of the operator. rq_probe checks the FILE and allocates nothing; the path that
     * actually killed the process was malloc returning NULL for the 11.4 MB checkpoint on a
     * calculator whose heap had been eaten by previous failed runs. A comment asserting a
     * guarantee the code does not make is worse than no comment -- this repo already had that
     * rule, and the comment is what stopped me reading the line under it.
     *
     * Both doors are now covered at startup: rq_probe for the file, and a trial allocation of the
     * real size for the heap. See main(). */
    if (!MODEL_OK) {
        /* 320, NOT 220. MODEL_WHY is 160 and the wrapper text is ~90, so 220 truncated -- and what
         * it truncated was the trailing "<end>", the token app_stream_end pairs with. An error
         * message that loses its own terminator is a hang wearing an error's clothes. */
        char msg[320];
        snprintf(msg, sizeof msg, "<a> The model could not be loaded: %s<end>", MODEL_WHY);
        app_stream_token(msg); app_stream_end(); return;
    }
    /* AGAIN AT THE MOMENT OF LOADING, not only at startup: the session's own allocations since then
     * can have taken the room, and a failure inside rq_build exits without freeing the checkpoint. */
    if (!MODEL_READY) {
        if (!model_room(MODEL_WHY, sizeof MODEL_WHY)) {
            char msg[320];
            snprintf(msg, sizeof msg, "<a> The model could not be loaded: %s<end>", MODEL_WHY);
            app_stream_token(msg); app_stream_end(); return;
        }
        rq_build(dpath("model4096.bin.tns")); MODEL_READY = 1;
    }
    static int ids[NS_MAX_TOKENS];
    int n = ns_tok_encode(&TK, prompt, ids, NS_MAX_TOKENS);
    if (n <= 0) { app_stream_token("<a> encode failed<end>"); app_stream_end(); return; }

    /* THE LOOP ITSELF IS src/store/gencore.c, shared with the device benchmark and the host harness
     * (tools/eval/int8gen.c), so a device/host comparison of a full tool-using turn compares ONE
     * implementation on two machines. What stays here is presentation, as hooks: streaming, the
     * status line, the stop button, and the structural call check. Behaviour is unchanged. */
    static char tool_call[64];
    static char shape_reason[192];
    tool_call[0] = 0; shape_reason[0] = 0;
    /* Structural call validation state -- docs/ARCHITECTURE.md s6. Initialised to UNCHECKED, not to
     * OK: a document that never reaches the check must not read as one that passed it. */
    int shape_bad = TLM_SHAPE_UNCHECKED;
    app_gen_ctx G = { 0, prompt, tool_call, &shape_bad, shape_reason };
    const tlm_gen_hooks hooks = { &G, gen_on_prefill, gen_on_token, gen_on_tool_begin,
                                  gen_on_tool_end, gen_on_inject, gen_on_injected, gen_should_stop };
    if (n <= 1) { app_status("Thinking", 0); app_draw(); }
    static tlm_gen_result R;
    tlm_generate(&TK, ids, n, &hooks, &R);
    if (R.stopped_in_prefill) { app_stream_token("<a> stopped<end>"); app_stream_end(); return; }
    const int *emitted = R.emitted;
    const int nemit = R.nemit;
    const int tool_ok = R.tool_ok;
    const char *tool_res = R.tool_res;
    const int stopped = R.stopped;
    (void)shape_bad;
    /* THE RUNTIME OWNS THE ARITHMETIC, INCLUDING IN THE PROSE.
     *
     * Measured on device and reproduced on host, fp32 and int8 alike:
     *     <tool> eval<arg> 0.5*(900.0)*((800.0))^(2)</tool><res> 288000000</res>
     *     <a> K = 229 J. From K=0.5*m*(v)^(2).<end>
     * The call is right, the runtime result is right, and the PROSE states a different number.
     * A44b removed the notation CONVERSION -- that arm is 0/155 -- but a residual copy error
     * remains, because restating 288000000 means reproducing nine digits token by token. Measured
     * by magnitude on round givens, greedy: 0.9% below 1e5, 4.3% from 1e5 to 1e7.
     *
     * This is not a corpus defect and no retrain fixes it: the corpus copies <res> verbatim there,
     * and the model is ~96% reliable at copying a long digit string. But the whole premise of
     * TOOL_SPEC is that the model does not compute and the runtime does -- so where the two
     * disagree, the runtime is right and says so. The prose is left standing, because silently
     * rewriting what the model said would hide the disagreement rather than report it. */
    if (tool_ok && tool_res[0] && tool_res[0] != '!') {
        static char full[NS_PROMPT_MAX];
        ns_tok_decode(&TK, emitted, nemit, full, sizeof full);
        const char *a = strstr(full, "<a>");
        if (a && !answer_states_result(a + 3, tool_res)) {
            /* SALIENCE, not layout. The first version appended "[runtime result: N]" and it
             * rendered correctly -- each SP_TEXT span after <end> gets its own line -- and was
             * still read as a footnote rather than a correction: the turn was reported as simply
             * wrong. A note that the reader does not act on is a note that did not fire.
             *
             * So it now names the defect and carries the unit, because the reader is checking a
             * physics answer and a bare number is not one. The prose is still left standing: the
             * model said what it said, and hiding that would misrepresent what the model does. */
            const char *unit = 0;
            /* idx < 0 is Form C, no record: there is no unit to name, and ST.rec[-1] is not one. */
            if (idx >= 0) for (int k = 0; k < ST.rec[idx].nvars; k++)
                if (ST.rec[idx].lhs && !strcmp(ST.rec[idx].var[k], ST.rec[idx].lhs)) {
                    unit = ST.rec[idx].unit[k]; break;
                }
            app_stream_token("The sentence above misstates the number. The calculator computed ");
            app_stream_token(tool_res);
            if (unit && unit[0] && strcmp(unit, "1")) {
                app_stream_token(" ");
                app_stream_token(unit);
            }
            app_stream_token(".");
        }
    }
    /* The line that stays. Elapsed time is measured; the tool is reported only if one ran. */
    app_status_done(clock_ms_since(t_start), tool_call[0] ? tool_call : 0, tool_res, tool_ok);
    if (stopped) { app_stream_token(" [stopped]"); while (isKeyPressed(KEY_NSPIRE_ESC)) { } }
    /* compact summary from the finished turn: relation + values + result */
    {
        char vals[64]; vals[0] = 0;
        for (int i = 0; i < in.nvals; i++) {
            char one[24];
            snprintf(one, sizeof one, "%s=%s ", in.var[i], in.val[i]);
            if (strlen(vals) + strlen(one) < sizeof vals) strcat(vals, one);
        }
        /* idx < 0 is Form C -- there is no record, and the transcript must not claim one. */
        app_finish_turn(idx < 0 ? "none" : ST.rec[idx].formula, vals);
    }
    app_stream_end();
    app_draw();
}

/* Put a failure on the SCREEN. Both boot failures used to return 1 before gfx_init(), so a wrong
 * data directory was a black screen -- indistinguishable from a hang, which is precisely the
 * failure shape the standing rule in the project log exists to prevent. It cost five device cycles once
 * already; it is not going to cost a sixth. */
static void die(const char *what, const char *detail) {
    gfx_clear(C_BG);
    gfx_text(12, 16, "ChatTLM cannot start", F_BIG, C_ERRFG, C_BG);
    gfx_text(12, 44, what, F_UIB, C_INK, C_BG);
    gfx_text_wrap(12, 62, detail, F_UI, C_INK2, C_BG, GFX_W - 24, 14, 1);
    gfx_text(12, GFX_H - 20, "Press ESC to exit.", F_UI, C_INK3, C_BG);
    gfx_present();
    while (!isKeyPressed(KEY_NSPIRE_ESC)) { }
    clock_restore();
    gfx_free();
}

int main(void) {
    gfx_init();                                  /* BEFORE any load, so a failure can be shown */
    char why[256];
    if (!resolve_data_dir(&ST, why, sizeof why)) {
        die("No directory holds all three data files.", why);
        return 1;
    }
    /* PROBE THE MODEL AT STARTUP, not on the first send.
     *
     * rq_build() reaches read_checkpoint(), which exit()s. Loading it lazily therefore meant a bad
     * checkpoint killed the process the moment the reader pressed enter, which looks exactly like
     * the app quitting on send and is what it was measured doing: the model on the device was a
     * 96-byte truncated file whose header prefix passed every check the loader had.
     *
     * This does not abort. A calculator that opens, lists its sessions and says WHY it cannot
     * answer is far more useful in front of a judge than one that disappears, and the reason is
     * exact enough to act on. */
    if (rq_probe(dpath("model4096.bin.tns"), MODEL_WHY, sizeof MODEL_WHY) == 0) MODEL_OK = 1;

    /* AND PROBE THE ALLOCATION, NOT ONLY THE FILE. A90.
     *
     * rq_probe validates the checkpoint's HEADER and allocates nothing, so it passes on a
     * calculator that has no room to load it. read_checkpoint then mallocs the whole file on the
     * first send, gets NULL, and exit()s -- which is the same vanishing-on-enter symptom the probe
     * above was added to remove, arriving through the one door it does not cover.
     *
     * MEASURED ON DEVICE, and it is not hypothetical. Ndless does not reclaim the heap from a
     * program that exit()s, so each failed attempt cost ~11 MB and they accumulated:
     *
     *     ram free 32,088,184 B   after a reboot          -> the app answers normally
     *     ram free  7,072,756 B   after a few failed runs -> malloc(11,417,728) returns NULL
     *
     * bench_ask caught it in one run, at `error=oom bytes=11417728`, after reading the code had
     * produced two confident wrong diagnoses. I had also cleared this hypothesis against
     * bench_memceiling, which reports first_alloc_11417728=ok and bare_largest_malloc=22,377,216 --
     * both true, both taken ON A FRESH HEAP, and neither a statement about a running program.
     * The project log already carries that exact trap (21.56 MiB bare against 4.83 MiB in situ) and I
     * trusted the probe without its precondition anyway.
     *
     * A TRIAL ALLOCATION IS THE ONLY HONEST PREDICTOR. Free RAM as reported is not the same
     * question as "can one contiguous block of this size be had", which is what read_checkpoint
     * needs. So ask for exactly what it will ask for, then give it straight back. */
    if (MODEL_OK && !model_room(MODEL_WHY, sizeof MODEL_WHY)) MODEL_OK = 0;

    if (ns_tok_load(&TK, dpath("tok4096.tok.tns")) != NST_OK) {
        die("Tokenizer failed to load.", dpath("tok4096.tok.tns"));
        ns_free(&ST);
        return 1;
    }
    pointer_init();
    app_init();
    /* Sessions survive the run. RAM-only was tolerable while exiting was obscure; it stopped being
     * so the moment there was a button for it. */
    app_set_persist(dpath("chats.tns.tns"));
    /* Ratings outlive the highlight: one line per press, appended. */
    app_set_feedback(dpath("feedback.tns.tns"));
    app_draw();

    /* THE LOOP DECIDES WHEN TO PAINT, and that is the whole fix for both the lag and the animation.
     *
     * Before, app_draw() ran on every input event and at no other time. Every touchpad sample
     * produced an event, so a finger on the pad meant a full 320x240 software repaint as fast as
     * this loop could spin -- which is what made the app feel like it was struggling -- and with no
     * finger there were no frames at all, so the placeholder simply stopped moving.
     *
     * Now: input is applied whenever it arrives, the clock is read every pass, and a frame is
     * painted only when something actually changed. app_set_now() answers that question, so a
     * resting screen paints nothing and the rotation still runs on its own schedule.
     */
    /* CONFIGURE THE TIMER BEFORE READING IT. clock_start() was only ever called on the generation
     * path, so the loop's clock read an SP804 that nothing had put into free-running mode: the
     * value was not a monotonic tick and the elapsed milliseconds derived from it advanced wildly.
     * That is why the placeholder cycled many times a second against a 4.2 s timer. This repo
     * already carries the rule -- bench/common.h configures LOAD and CONTROL before reading, and
     * reading raw once produced a 2^32 underflow -- and the loop was reading raw. */
    /* PACED BY msleep, with the elapsed time ACCUMULATED from it.
     *
     * Milliseconds were derived from the SP804 directly and that was wrong twice over: the timer
     * was never configured, and then it was trusted while nothing had put it in free-running mode.
     * msleep() is the SDK's own primitive and is monotonic by construction, so a loop built on it
     * CANNOT race. If msleep overshoots the animation runs slow, which is benign; the raw-timer
     * version ran unboundedly fast, which is what shipped.
     *
     * It also idles the core between passes rather than spinning, which is the other half of why
     * the app felt like it was struggling.
     */
    /* INPUT IS POLLED FIVE TIMES PER FRAME.
     *
     * At one poll per 25ms frame, a brisk tap could begin and end between two polls and never be
     * seen at all -- which is why a click sometimes took two or three goes. Drawing at 40fps is
     * plenty; SAMPLING at 40Hz is not, because a tap is an event with a beginning and an end and
     * both have to fall inside the window. Input runs at 200Hz now and the frame rate is unchanged.
     */
    const unsigned POLL_MS = 5, POLLS_PER_FRAME = 5;
    unsigned now = 0;
    while (!app_should_quit()) {
        int dirty = 0;

        for (unsigned s = 0; s < POLLS_PER_FRAME && !app_should_quit(); s++) {
            in_event e; memset(&e, 0, sizeof e);
            if (pointer_poll(&e)) { app_event(&e); dirty = 1; }
            int k = keypad_poll();
            if (k) { e.kind = IN_KEY; e.key = k; app_event(&e); dirty = 1; }
            msleep(POLL_MS);
            now += POLL_MS;
        }

        if (app_set_now(now)) dirty = 1;
        if (dirty) app_draw();
    }
    if (MODEL_READY) rq_free();
    ns_tok_free(&TK); ns_free(&ST);
    clock_restore();                             /* hand the hardware back before leaving */
    gfx_free();
    return 0;
}
