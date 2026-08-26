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
#include "loader.h"
#include "assemble.h"
#include "tokenizer.h"
#include "../../tools/eval/eval.h"
#include "toolrun.h"

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

static void clock_start(void) {
    /* Only configure it if nobody else is running it: an already-enabled free-running timer is
     * almost certainly the OS's, and stopping it would be rude and probably fatal. */
    uint32_t c = TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_CTRL);
    if (!((c & (1u << 7)) && (c & (1u << 1)))) {
        TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_CTRL) = 0;
        TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_LOAD) = 0xFFFFFFFFu;
        TLM_MMIO32(TLM_TIMER_32K + TLM_SP804_CTRL) = (1u << 7) | (1u << 1);
    }
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

static void pointer_init(void) {
    touchpad_info_t *ti = touchpad_getinfo();
    if (ti && ti->width && ti->height) { PAD_W = ti->width; PAD_H = ti->height; }
}

/* Returns 1 if an event was produced.
 *
 * RELATIVE motion, not absolute. The pad reports where the finger IS, and mapping that straight to
 * a screen position makes the cursor teleport to wherever you happen to touch down, then snap back
 * the moment you lift and re-touch -- which is what it did. A pad this small cannot be an absolute
 * digitiser anyway: it is ~1/4 the aspect of the panel, so absolute mapping also means you can only
 * reach a screen position by touching the corresponding fraction of a 2 cm pad.
 *
 * So it behaves like a trackpad. Contact establishes a reference point and does NOT move the
 * cursor; subsequent motion moves it by the delta. Lifting clears the reference, so the next touch
 * resumes from where the cursor already is instead of jumping.
 *
 * Tap versus drag is unchanged and still needed: app.c has handled IN_SCROLL since it was written
 * and nothing ever emitted one.
 */
#define TAP_SLOP  5             /* pixels of cursor travel still counted as a tap */
#define PAD_GAIN  3             /* cursor pixels per pad unit, x256 -- tuned for a 320px panel */

static int DOWN, HAVE_REF, REF_X, REF_Y, TRAVEL;
/* Sub-pixel remainder. The pad is higher-resolution than the panel, so `delta * GFX_W / PAD_W`
 * truncates every small movement to ZERO -- slow, precise motion would move the cursor not at all
 * while fast motion worked, which reads as a dead pad rather than as a scaling bug. Carrying the
 * remainder in 1/256ths makes fine motion accumulate instead of being discarded. */
static int ACC_X, ACC_Y;

static int pointer_poll(in_event *e) {
    touchpad_report_t r;
    if (touchpad_scan(&r) != 0) return 0;

    int active = r.contact || r.proximity;
    if (!active) {
        /* Lift. Drop the reference so the next touch does not jump, and turn a short press into a
         * click. */
        HAVE_REF = 0;
        if (DOWN) {
            DOWN = 0;
            e->x = CX; e->y = CY; e->hover = 0;
            if (TRAVEL <= TAP_SLOP) { e->kind = IN_CLICK; return 1; }
        }
        return 0;
    }

    if (!HAVE_REF) {                      /* first sample of a touch: anchor, do not move */
        HAVE_REF = 1; REF_X = r.x; REF_Y = r.y; TRAVEL = 0; ACC_X = ACC_Y = 0;
        if (r.contact) DOWN = 1;
        e->x = CX; e->y = CY; e->hover = r.proximity && !r.contact; e->kind = IN_MOVE;
        return 1;
    }

    /* Pad coordinates are scaled to the panel so a full swipe crosses roughly the full screen,
     * then multiplied by a gain. Pad y is bottom-up; the screen is top-down. */
    ACC_X += (r.x - REF_X) * GFX_W * 128 * PAD_GAIN / (PAD_W ? PAD_W : 1);
    ACC_Y += (REF_Y - r.y) * GFX_H * 128 * PAD_GAIN / (PAD_H ? PAD_H : 1);
    REF_X = r.x; REF_Y = r.y;
    int dx = ACC_X / 256, dy = ACC_Y / 256;
    ACC_X -= dx * 256; ACC_Y -= dy * 256;      /* keep the remainder, do not discard it */

    if (r.contact) {
        DOWN = 1;
        TRAVEL += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        /* A press that is moving vertically scrolls; the cursor stays put. */
        if (dy > 1 || dy < -1) {
            e->x = CX; e->y = CY; e->hover = 0;
            e->kind = IN_SCROLL; e->dy = -dy;
            return 1;
        }
    }

    CX += dx; CY += dy;
    if (CX < 0) CX = 0;
    if (CX >= GFX_W) CX = GFX_W - 1;
    if (CY < 0) CY = 0;
    if (CY >= GFX_H) CY = GFX_H - 1;
    e->x = CX; e->y = CY;
    e->hover = r.proximity && !r.contact;
    e->kind = IN_MOVE;
    return 1;
}

/* ---- keypad ---------------------------------------------------------------------------------- */
static int keypad_poll(void) {
    /* Ctrl first, and as a CHORD rather than a modifier flag: ctrl+N and ctrl+S are the only two,
     * and returning them as their own codes keeps app.c from knowing anything about the keypad.
     * Checked before the plain map or ctrl+N would arrive as a bare 'n' in the composer. */
    if (isKeyPressed(KEY_NSPIRE_CTRL)) {
        if (isKeyPressed(KEY_NSPIRE_N)) {
            while (isKeyPressed(KEY_NSPIRE_N)) { }
            return K_NEW;
        }
        if (isKeyPressed(KEY_NSPIRE_S)) {
            while (isKeyPressed(KEY_NSPIRE_S)) { }
            return K_SEARCH;
        }
    }
    static const struct { const t_key *k; int c; } MAP[] = {
        { &KEY_NSPIRE_ESC, K_ESC }, { &KEY_NSPIRE_ENTER, K_ENTER }, { &KEY_NSPIRE_TAB, K_TAB },
        { &KEY_NSPIRE_DEL, K_BACK }, { &KEY_NSPIRE_UP, K_UP }, { &KEY_NSPIRE_DOWN, K_DOWN },
        { &KEY_NSPIRE_SPACE, ' ' }, { &KEY_NSPIRE_PERIOD, '.' },
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
    for (unsigned i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
        if (isKeyPressed(*MAP[i].k)) {
            while (isKeyPressed(*MAP[i].k)) { }      /* debounce to one edge */
            return MAP[i].c;
        }
    }
    return 0;
}

/* ---- generation ------------------------------------------------------------------------------- */
static int argmax(const float *v, int n) { int b = 0; for (int i = 1; i < n; i++) if (v[i] > v[b]) b = i; return b; }

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
static const char *DATA_DIRS[] = { "/documents/tlm/", "/documents/slm/", "/documents/ndless/",
                                   "/documents/bench/", "/documents/" };
static char DATA_DIR[32];

static const char *dpath(const char *leaf) {
    static char buf[80];
    snprintf(buf, sizeof buf, "%s%s", DATA_DIR, leaf);
    return buf;
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

void app_request(const char *question, const char *rid) {
    (void)rid;
    app_begin_turn(question);
    app_draw();

    /* pick the first record whose name appears in the question, else the first record.
     * Retrieval measured 8.0% on real questions, so this is a placeholder until the picker UI
     * lands -- it is NOT a retrieval claim. */
    int idx = 0;
    static char prompt[NS_PROMPT_MAX];
    ns_input in; in.nvals = 0;

    /* SESSION CONTEXT, compacted. The model is single-turn -- every training document is one
     * <q>..</q><r>..<a>..<end> and it has never seen a conversation -- so prior turns go in as
     * plain text inside the question rather than as extra document structure.
     *
     * Budget in CHARACTERS rather than tokens: counting tokens costs a full BPE pass per keystroke
     * on a 396 MHz core. 4.15 chars/token measured on this corpus, and the budget below is derived
     * from the 167-token context allowance that survives the record and question.
     */
    static char withctx[NS_PROMPT_MAX];
    char ctx[512];
    int rec_tok = 38, q_tok = (int)strlen(question) / 4;
    int budget_tok = 256 - rec_tok - q_tok - 24;
    if (budget_tok < 0) budget_tok = 0;
    int budget_chars = budget_tok * 4;
    if (budget_chars > (int)sizeof ctx - 1) budget_chars = (int)sizeof ctx - 1;
    app_context(ctx, sizeof ctx, budget_chars);
    snprintf(withctx, sizeof withctx, "%s%s", ctx, question);
    question = withctx;

    if (ns_assemble(prompt, sizeof prompt, &ST.rec[idx], question, &in) < 0) {
        app_stream_token("<a> could not assemble a prompt<end>"); app_stream_end(); return;
    }
    clock_start();
    uint32_t t_start = clock_raw();
    app_status("Reading", ST.rec[idx].name);
    app_draw();

    if (!MODEL_READY) { rq_build(dpath("model4096.bin.tns")); MODEL_READY = 1; }
    int V = rq_vocab();

    static int ids[NS_MAX_TOKENS];
    int n = ns_tok_encode(&TK, prompt, ids, NS_MAX_TOKENS);
    if (n <= 0) { app_stream_token("<a> encode failed<end>"); app_stream_end(); return; }

    /* Token ids the runtime steers on. Looked up by TEXT, so the device and the host steer on the
     * same tokens rather than on numbers hardcoded twice. */
    const int ID_RES   = ns_tok_special_id(&TK, "<res>");
    const int ID_TOOLC = ns_tok_special_id(&TK, "</tool>");
    const int ID_END   = ns_tok_special_id(&TK, "<end>");

    int tok = ids[0], pos = 0;
    while (pos < n - 1) { rq_forward(tok, pos); pos++; tok = ids[pos]; }

    static int emitted[300];                 /* what the model has produced, for span extraction */
    int nemit = 0;

    /* POLL. This loop used to run to completion with nothing checking for input: 60 tokens at the
     * measured 2.683 tok/s is 22.4 seconds during which the calculator answered no key and no tap.
     * In front of a judge that is not "slow", it is indistinguishable from a crash -- and it is the
     * one code path where the device is guaranteed to look broken while working perfectly.
     *
     * Polling between tokens, not inside rq_forward, so the cost is one keypad scan per ~370 ms of
     * compute rather than anything measurable against the forward pass. */
    int stopped = 0;
    static char tool_call[64], tool_res[48];
    int tool_ok = 0; tool_call[0] = 0; tool_res[0] = 0;
    app_status("Thinking", 0);
    app_draw();

    int wrote = 0;
    for (int s = 0; s < 90 && pos < 250; s++) {
        float *lg = rq_forward(tok, pos); pos++;

        /* The model may not SUPPLY its own result. <res> is the runtime's to emit, and suppressing
         * the logit is what makes that structural rather than a convention the model may break.
         * The host has done this since it was written (server.py: lg[0, RES] = -1e30); the device
         * did not, which is half of why its result chip held a number nobody had checked. */
        if (ID_RES >= 0) lg[ID_RES] = -1e30f;

        tok = argmax(lg, V);
        if (nemit < (int)(sizeof emitted / sizeof emitted[0])) emitted[nemit++] = tok;

        char piece[64];
        ns_tok_decode(&TK, &tok, 1, piece, sizeof piece);
        app_stream_token(piece);
        if (!wrote && strstr(piece, "<a>")) { wrote = 1; app_status("Writing the answer", 0); }
        app_draw();                          /* stream: one repaint per token */
        if (tok == ID_END || tok == 10) break;

        /* ---- a call just closed: EXECUTE IT ------------------------------------------------
         * This is the whole architecture. The model emitted a call; the runtime runs it and feeds
         * the real answer back in, so everything the model writes afterwards is composed around a
         * number it did not choose. */
        if (tok == ID_TOOLC) {
            static char doc[1024], span[320], inj[MAX_RESULT + 16];
            ns_tok_decode(&TK, emitted, nemit, doc, sizeof doc);
            if (tlm_extract_call(doc, span, sizeof span)) {
                /* Say what is being run BEFORE running it: on a 396 MHz core the evaluator is fast
                 * but not instant, and a frozen screen with no explanation is the failure mode this
                 * whole line exists to prevent. */
                tlm_call_label(span, tool_call, sizeof tool_call);
                app_status("Running", tool_call);
                app_draw();
                tool_ok = tlm_result_span(span, inj, sizeof inj);
                {   const char *o = strstr(inj, "<res>");
                    const char *c = o ? strstr(o, "</res>") : 0;
                    if (o && c) {
                        int L = (int)(c - o) - 5;
                        if (L >= (int)sizeof tool_res) L = (int)sizeof tool_res - 1;
                        if (L > 0) { memcpy(tool_res, o + 5, (size_t)L); tool_res[L] = 0; }
                    } }
                app_status(tool_ok ? "Got" : "Tool refused", tool_res);
                app_draw();
            } else
                /* Malformed span. Report the evaluator's own no-call code rather than skipping, so
                 * a broken call is visible instead of looking like a clean answer. */
                snprintf(inj, sizeof inj, "<res>!give</res>");

            int iids[64];
            int ni = ns_tok_encode(&TK, inj, iids, 64);
            for (int k = 0; k < ni && pos < 250; k++) {
                rq_forward(tok, pos); pos++;         /* consume the token just produced */
                tok = iids[k];
                if (nemit < (int)(sizeof emitted / sizeof emitted[0])) emitted[nemit++] = tok;
                ns_tok_decode(&TK, &tok, 1, piece, sizeof piece);
                app_stream_token(piece);
            }
            app_draw();
        }

        if (isKeyPressed(KEY_NSPIRE_ESC)) { stopped = 1; }
        else {
            /* A tap on Stop counts the same as the key. This asks where the CURSOR is, not where
             * the finger is: pointing is relative now, so pad coordinates no longer name a screen
             * position at all. Mapping them as if they did would have made Stop respond to a touch
             * in the corresponding corner of the pad while ignoring a tap with the cursor sitting
             * on the button -- the exact inversion of what the user sees. */
            touchpad_report_t r;
            if (touchpad_scan(&r) == 0 && r.contact && app_hit_stop(CX, CY)) stopped = 1;
        }
        if (app_take_abort()) stopped = 1;
        if (stopped) break;
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
        app_finish_turn(ST.rec[idx].formula, vals);
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
    gfx_free();
}

int main(void) {
    gfx_init();                                  /* BEFORE any load, so a failure can be shown */
    char why[256];
    if (!resolve_data_dir(&ST, why, sizeof why)) {
        die("No directory holds all three data files.", why);
        return 1;
    }
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
    app_draw();

    while (!app_should_quit()) {
        in_event e; memset(&e, 0, sizeof e);
        if (pointer_poll(&e)) { app_event(&e); app_draw(); }
        int k = keypad_poll();
        if (k) { e.kind = IN_KEY; e.key = k; app_event(&e); app_draw(); }
    }
    if (MODEL_READY) rq_free();
    ns_tok_free(&TK); ns_free(&ST);
    gfx_free();
    return 0;
}
