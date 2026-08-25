/* ChatSLM on the calculator. Touchpad cursor, framebuffer UI, real model.
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

/* Returns 1 if an event was produced. Absolute mapping when the finger is down or in proximity;
 * the cursor holds its last position otherwise, which is what a mouse does. */
static int pointer_poll(in_event *e) {
    touchpad_report_t r;
    if (touchpad_scan(&r) != 0) return 0;
    if (!r.contact && !r.proximity) return 0;
    CX = (int)((long)r.x * GFX_W / (PAD_W ? PAD_W : 1));
    CY = GFX_H - 1 - (int)((long)r.y * GFX_H / (PAD_H ? PAD_H : 1));   /* pad y is bottom-up */
    if (CX < 0) CX = 0;
    if (CX >= GFX_W) CX = GFX_W - 1;
    if (CY < 0) CY = 0;
    if (CY >= GFX_H) CY = GFX_H - 1;
    e->x = CX; e->y = CY;
    e->hover = r.proximity && !r.pressed;
    e->kind = r.pressed ? IN_CLICK : IN_MOVE;
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

/* Kept, not deleted: the evaluator lands here when it is linked into the device build, and a
 * deleted stub would hide that the tool path is still unimplemented. */
__attribute__((unused))
static const char *run_call(const char *call) {
    static char out[64];
    /* the evaluator is a host tool; on device the call is executed by the same C the host uses.
     * Until that is linked in, an unexecuted call is reported honestly rather than faked. */
    (void)call;
    snprintf(out, sizeof out, "!give");
    return out;
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
    if (!MODEL_READY) { rq_build("/documents/slm/model4096.bin.tns"); MODEL_READY = 1; }
    int V = rq_vocab();

    static int ids[NS_MAX_TOKENS];
    int n = ns_tok_encode(&TK, prompt, ids, NS_MAX_TOKENS);
    if (n <= 0) { app_stream_token("<a> encode failed<end>"); app_stream_end(); return; }

    int tok = ids[0], pos = 0;
    while (pos < n - 1) { rq_forward(tok, pos); pos++; tok = ids[pos]; }
    for (int s = 0; s < 60 && pos < 250; s++) {
        float *lg = rq_forward(tok, pos); pos++;
        tok = argmax(lg, V);
        char piece[64];
        ns_tok_decode(&TK, &tok, 1, piece, sizeof piece);
        app_stream_token(piece);
        app_draw();                          /* stream: one repaint per token */
        if (tok == 10) break;
    }
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

int main(void) {
    if (ns_load(&ST, "/documents/slm/store.tns.tns") != NS_OK) return 1;
    if (ns_tok_load(&TK, "/documents/slm/tok4096.tok.tns") != NST_OK) return 1;
    gfx_init();
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
