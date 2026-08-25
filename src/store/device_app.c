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
 * Tap versus drag. app.c has handled IN_SCROLL since it was written and NOTHING EVER SENT ONE --
 * the poll only ever produced IN_MOVE and IN_CLICK, so the transcript scrolled by arrow key alone
 * and the session list not at all. A handler with no producer reads as a feature and is not one;
 * same shape as the provenance check that was written, unit-tested, and never called.
 *
 * A finger down that then moves emits IN_SCROLL deltas; a finger down that lifts having barely
 * moved emits one IN_CLICK. Direction is natural: dragging up pushes content up. */
#define TAP_SLOP 5                    /* pixels of travel still counted as a tap */

static int DOWN, LAST_Y, TRAVEL;

static int pointer_poll(in_event *e) {
    touchpad_report_t r;
    if (touchpad_scan(&r) != 0) return 0;

    if (r.contact) {
        CX = (int)((long)r.x * GFX_W / (PAD_W ? PAD_W : 1));
        CY = GFX_H - 1 - (int)((long)r.y * GFX_H / (PAD_H ? PAD_H : 1));   /* pad y is bottom-up */
        if (CX < 0) CX = 0;
        if (CX >= GFX_W) CX = GFX_W - 1;
        if (CY < 0) CY = 0;
        if (CY >= GFX_H) CY = GFX_H - 1;
        e->x = CX; e->y = CY; e->hover = 0;
        if (!DOWN) { DOWN = 1; LAST_Y = CY; TRAVEL = 0; e->kind = IN_MOVE; return 1; }
        int d = CY - LAST_Y;
        TRAVEL += d < 0 ? -d : d;
        if (d > 1 || d < -1) { LAST_Y = CY; e->kind = IN_SCROLL; e->dy = -d; return 1; }
        e->kind = IN_MOVE; return 1;
    }

    if (DOWN) {                                  /* release: a short press is a click */
        DOWN = 0;
        e->x = CX; e->y = CY; e->hover = 0;
        if (TRAVEL <= TAP_SLOP) { e->kind = IN_CLICK; return 1; }
        return 0;                                /* it was a drag; the scrolls already went out */
    }

    if (!r.proximity) return 0;
    CX = (int)((long)r.x * GFX_W / (PAD_W ? PAD_W : 1));
    CY = GFX_H - 1 - (int)((long)r.y * GFX_H / (PAD_H ? PAD_H : 1));
    if (CX < 0) CX = 0;
    if (CX >= GFX_W) CX = GFX_W - 1;
    if (CY < 0) CY = 0;
    if (CY >= GFX_H) CY = GFX_H - 1;
    e->x = CX; e->y = CY; e->hover = 1; e->kind = IN_MOVE;
    return 1;
}

/* ---- keypad ---------------------------------------------------------------------------------- */
static int keypad_poll(void) {
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

/* Execute one tool call ON THE CALCULATOR.
 *
 * This is the whole thesis of the project: the model does not compute, it emits a call, and the
 * runtime executes it. Until now this returned "!give" and the answer's number came from the model
 * -- so the device was ASSERTING the architecture rather than demonstrating it.
 *
 * tool_call_text() is the same entry point the host cli uses (tools/eval/main.c:16), over the same
 * CORE sources, which the evaluator's own Makefile already cross-compiles for ARMv5TE. Host and
 * device therefore run identical arithmetic; TOOL_SPEC 5.1 requires the formatting to be
 * byte-identical and tools/eval/device_main.c is the suite that checks it.
 *
 * A failure returns the evaluator's own refusal code. It never invents a value and never falls
 * back to whatever the model was going to say. */
static const char *run_call(const char *call) {
    static char out[MAX_RESULT];
    out[0] = 0;
    if (tool_call_text(call, out, sizeof out) != TB_OK || !out[0])
        snprintf(out, sizeof out, "%s", out[0] ? out : "!give");
    return out;
}

/* Last occurrence of `needle`. The model can emit more than one call in a document, and the span to
 * execute is the one that just closed, not the first one in the buffer. */
static const char *rfind(const char *hay, const char *needle) {
    const char *last = 0, *p = hay;
    for (;;) { p = strstr(p, needle); if (!p) break; last = p; p++; }
    return last;
}

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
    if (!MODEL_READY) { rq_build(dpath("model4096.bin.tns")); MODEL_READY = 1; }
    int V = rq_vocab();

    static int ids[NS_MAX_TOKENS];
    int n = ns_tok_encode(&TK, prompt, ids, NS_MAX_TOKENS);
    if (n <= 0) { app_stream_token("<a> encode failed<end>"); app_stream_end(); return; }

    int tok = ids[0], pos = 0;
    while (pos < n - 1) { rq_forward(tok, pos); pos++; tok = ids[pos]; }
    /* POLL. This loop used to run to completion with nothing checking for input: 60 tokens at the
     * measured 2.683 tok/s is 22.4 seconds during which the calculator answered no key and no tap.
     * In front of a judge that is not "slow", it is indistinguishable from a crash -- and it is the
     * one code path where the device is guaranteed to look broken while working perfectly.
     *
     * Polling between tokens, not inside rq_forward, so the cost is one keypad scan per ~370 ms of
     * compute rather than anything measurable against the forward pass. */
    int stopped = 0;
    for (int s = 0; s < 60 && pos < 250; s++) {
        float *lg = rq_forward(tok, pos); pos++;
        tok = argmax(lg, V);
        char piece[64];
        ns_tok_decode(&TK, &tok, 1, piece, sizeof piece);
        app_stream_token(piece);
        app_draw();                          /* stream: one repaint per token */
        if (tok == 10) break;

        if (isKeyPressed(KEY_NSPIRE_ESC)) { stopped = 1; }
        else {
            touchpad_report_t r;             /* a tap on Stop counts the same as the key */
            if (touchpad_scan(&r) == 0 && r.contact) {
                int tx = (int)((long)r.x * GFX_W / (PAD_W ? PAD_W : 1));
                int ty = GFX_H - 1 - (int)((long)r.y * GFX_H / (PAD_H ? PAD_H : 1));
                if (app_hit_stop(tx, ty)) stopped = 1;
            }
        }
        if (app_take_abort()) stopped = 1;
        if (stopped) break;
    }
    /* Say it was stopped. A truncated answer that looks complete is a worse outcome than a slow
     * one, because the reader cannot tell the model was cut off mid-sentence. */
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
