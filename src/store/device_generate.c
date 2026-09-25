/* THE RUN: store -> picker -> assemble -> our tokenizer -> model -> tokens.
 *
 * Uses OUR tokenizer (byte-exact on this device, 9/9 vs host) rather than llama2.c's, because
 * ours is what the corpus was built with. The model is driven through runq_nspire's forward()
 * and sample(), which measured 1.694 tok/s on this hardware with a different checkpoint.
 *
 * Greedy (temperature 0) so the output is REPRODUCIBLE and comparable to the host. Sampling
 * would make a host/device mismatch unfalsifiable.
 *
 * Timed with the SP804 32 kHz timer via libndls, on battery. Any number taken over USB is at
 * 288 MHz and invalid. */
#include <libndls.h>
#include "nspire_screen.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "loader.h"
#include "assemble.h"
#include "tokenizer.h"
#include "askparse.h"
#include "gencore.h"

typedef struct { int dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size, seq_len; } CfgV;
/* from runq_nspire.c */
typedef struct Transformer Transformer;
extern int   rq_seqlen(void);
extern void  rq_build(const char *path);
extern float *rq_forward(int token, int pos);
extern int   rq_vocab(void);
extern void  rq_free(void);

FILE *g_nspire_log = 0;   /* the engine writes progress here; defined by whoever links it */
static FILE *LOG;
static void say(const char *f, ...) {
    char b[600]; va_list a; va_start(a,f); vsnprintf(b,sizeof b,f,a); va_end(a);
    screen_printf("%s\n", b);
    if (LOG) { fputs(b,LOG); fputc('\n',LOG); fflush(LOG); }
}
static int argmax(const float *v, int n) {
    int bi = 0; float bv = v[0];
    for (int i = 1; i < n; i++) if (v[i] > bv) { bv = v[i]; bi = i; }
    return bi;
}

/* ---- A152: timing hooks for the whole-turn parity phase ---------------------------------------
 * The 32 kHz timer counts DOWN; main() configures it before any turn runs. */
static volatile uint32_t TURN_T0, TURN_TFIRST;
static volatile int TURN_FIRST;
static void turn_on_token(void *ctx, int tok) {
    (void)ctx; (void)tok;
    if (!TURN_FIRST) { TURN_FIRST = 1; TURN_TFIRST = *(volatile uint32_t *)(uintptr_t)0x900D0004u; }
}
static const tlm_gen_hooks TURN_HOOKS = { 0, 0, turn_on_token, 0, 0, 0, 0, 0 };

int main(void) {
    screen_init();
static char GDIR[32] = "/documents/tlm/";
    /* A144. APPEND, NOT OVERWRITE. The timing needs three battery runs for a spread, and "w" kept only
     * the last -- two runs' results would have existed only as photographs. Each run is marked so
     * the host can split them; the file stays a few KB, far below the size that has failed to pull. */
    LOG = fopen("/documents/tlm/genlog.txt.tns", "a");
    g_nspire_log = LOG;
    say("\n== generate ==");
    say("printf works from this TU");

    ns_store2 st; ns_tok tk;
    {   static const char *D[] = { "/documents/chattlm/", "/documents/tlm/", "/documents/slm/", "/documents/ndless/" };
        int found = 0;
        for (unsigned i = 0; i < sizeof D / sizeof D[0] && !found; i++) {
            char pb[80]; snprintf(pb, sizeof pb, "%sstore.tns.tns", D[i]);
            if (ns_load(&st, pb) == NS_OK) { snprintf(GDIR, sizeof GDIR, "%s", D[i]); found = 1; }
        }
        if (!found) { say("STOP: store"); goto done; }
    }
    {   char pb[80]; snprintf(pb, sizeof pb, "%stok4096.tok.tns", GDIR);
        if (ns_tok_load(&tk, pb) != NST_OK) { say("STOP: tok"); goto done; } }
    say("store %d records, vocab %d", st.n, tk.nvocab);

    int idx = -1;
    for (int i = 0; i < st.n; i++) if (!strcmp(st.rec[i].formula, "v=d/t")) { idx = i; break; }
    if (idx < 0) { say("STOP: record"); goto done; }
    static char prompt[NS_PROMPT_MAX];
    ns_input in = {{"d","t"},{"150","12"},2};
    ns_assemble(prompt, sizeof prompt, &st.rec[idx],
                "A car goes 150 m in 12 s. Find the speed.", &in);
    say("prompt: %s", prompt);

    static int ids[NS_MAX_TOKENS];
    int n = ns_tok_encode(&tk, prompt, ids, NS_MAX_TOKENS);
    say("prompt tokens: %d", n);
    if (n <= 0) { say("STOP: encode"); goto done; }

    say("loading model...");
    {   char pb[80]; snprintf(pb, sizeof pb, "%smodel4096.bin.tns", GDIR); rq_build(pb); }
    int V = rq_vocab();
    say("model loaded, vocab %d", V);

    /* ---- TIMER, configured before use -------------------------------------------------------
     * The first run read the SP804 register raw, without setting LOAD or CONTROL: prefill came
     * back as 7 ticks (0.2 ms for 51 forward passes) and decode underflowed to 2^32-12. An
     * uninitialised down-counter has no defined content. bench/common.h does this correctly and
     * I did not read it -- the same mistake as the transfer paths.
     *
     * 32.768 kHz crystal timer, free-running, 32-bit, no prescale. Independent of the CPU clock,
     * so the tick rate is right whether the core is at 396 or 288 MHz. */
    #define MMIO32(a) (*(volatile uint32_t *)(uintptr_t)(a))
    #define T32   0x900D0000u
    #define T_LOAD 0x00u
    #define T_VAL  0x04u
    #define T_CTRL 0x08u
    #define CTRL_FREERUN 0x82u          /* ENABLE(1<<7) | 32BIT(1<<1) */
    uint32_t saved_ctrl = MMIO32(T32 + T_CTRL), saved_load = MMIO32(T32 + T_LOAD);
    MMIO32(T32 + T_CTRL) = 0;
    MMIO32(T32 + T_LOAD) = 0xFFFFFFFFu;
    MMIO32(T32 + T_CTRL) = CTRL_FREERUN;
    /* prove the clock is actually ticking before trusting a single measurement */
    uint32_t c1 = MMIO32(T32 + T_VAL);
    for (volatile int i = 0; i < 200000; i++) { }
    uint32_t c2 = MMIO32(T32 + T_VAL);
    say("timer: %s (%u ticks over a busy loop)", (c1 - c2) ? "RUNNING" : "DEAD", c1 - c2);
    if (!(c1 - c2)) { say("STOP: timer dead, any tok/s would be fiction"); goto restore; }

    /* CPU clock, so a run accidentally taken over USB is self-identifying rather than a quietly
     * wrong number. 288 MHz means USB was plugged in and the result is INVALID. */
    { uint32_t m = MMIO32(0x90140030u);
      unsigned mult = (m >> 24) & 0xFF, div1 = (m >> 16) & 0x3F;
      unsigned ahb = div1 ? (12u * mult / div1) : 0;
      say("AHB %u MHz -> CPU ~%u MHz %s", ahb, ahb * 2,
          (ahb * 2 >= 380) ? "(battery, VALID)" : "(USB? INVALID if <380)"); }

    int tok = ids[0], pos = 0, produced = 0;
    /* A142. SIZED TO THE CAP. This was out[64] while the loop below ran to 120 -- a 56-int
     * overrun on a device with no memory protection, which would have corrupted whatever
     * followed it rather than faulting. Caught before the push, by reading the loop bound and
     * the buffer together. */
    #define GEN_MAX 120
    static int out[GEN_MAX];
    uint32_t t0 = MMIO32(T32 + T_VAL);
    while (pos < n - 1) { rq_forward(tok, pos); pos++; tok = ids[pos]; }
    uint32_t t1 = MMIO32(T32 + T_VAL);
    /* A142. 120, NOT 24. Twenty-four tokens compared host-to-device 24/24 identical, which
     * is the right measurement and too small a sample to bound anything: a 0.048 logit
     * deviation flips an argmax whenever the top two candidates are within it, and 24
     * draws cannot see a tail that thin. 120 costs about a minute more of decode and is
     * the same run. The tok/s figure is unaffected -- it is a rate, computed over whatever
     * number of tokens were produced. */
    /* A146. WHERE DOES THE DECODE TIME GO? The cost model built from bench_forward under-predicts
     * this loop by a consistent 4.3-4.5% (~22-23 ms per token) on two unseen runs. Two candidate
     * causes, and this separates them: time spent INSIDE rq_forward, and time spent in the argmax
     * over the logits (soft-float comparisons, outside rq_forward). If the forward alone accounts for
     * the gap, the model under-predicts the forward pass itself; if the argmax does, it does not. */
    uint32_t fwd_ticks = 0, am_ticks = 0;
    for (int s2 = 0; s2 < GEN_MAX; s2++) {
        uint32_t a0 = MMIO32(T32 + T_VAL);
        float *lg = rq_forward(tok, pos);
        uint32_t a1 = MMIO32(T32 + T_VAL);
        pos++;
        tok = argmax(lg, V);
        uint32_t a2 = MMIO32(T32 + T_VAL);
        fwd_ticks += a0 - a1; am_ticks += a1 - a2;       /* down-counter */
        out[produced++] = tok;
        if (tok == 10) break;
    }
    uint32_t t2 = MMIO32(T32 + T_VAL);

    uint32_t dpre = t0 - t1, ddec = t1 - t2;      /* down-counter: start - end */
    say("prefill %d tokens: %u ticks = %u.%03u s", n - 1, dpre,
        dpre / 32768u, (dpre % 32768u) * 1000u / 32768u);
    say("decode  %d tokens: %u ticks = %u.%03u s", produced, ddec,
        ddec / 32768u, (ddec % 32768u) * 1000u / 32768u);
    if (ddec) {
        uint32_t milli = (uint32_t)((unsigned long long)produced * 32768ull * 1000ull / ddec);
        say("DECODE THROUGHPUT: %u.%03u tok/s", milli / 1000u, milli % 1000u);
    }
    say("decode split: in rq_forward %u ticks = %u ms, in argmax %u ticks = %u ms, other %d ticks",
        fwd_ticks, (unsigned)((unsigned long long)fwd_ticks * 1000u / 32768u),
        am_ticks, (unsigned)((unsigned long long)am_ticks * 1000u / 32768u),
        (int)ddec - (int)fwd_ticks - (int)am_ticks);
    /* Room for GEN_MAX ids. At 300 bytes with a 270-byte guard this silently truncated the id
     * list, which is the one artefact the host comparison needs verbatim. */
    { static char line[GEN_MAX * 7]; int p2 = 0;
      for (int i = 0; i < produced && p2 < (int)sizeof line - 8; i++)
          p2 += snprintf(line + p2, sizeof line - p2, "%d ", out[i]);
      say("ids: %s", line); }
    { static char txt[4096];
      ns_tok_decode(&tk, out, produced, txt, sizeof txt);
      say("TEXT: %s", txt); }
    /* A152. PARITY OF THE WHOLE TURN, NOT OF FREE-RUNNING DECODE.
     *
     * The A146 phase decoded five prompts with no tool execution, so it checked the engine and not
     * the loop the answers come from: tool calls executed, results injected, refusals and
     * explanations written. Each turn below is assembled
     * as app_request assembles it (ask_build, then ns_assemble on the chosen record) and run through
     * tlm_generate -- src/store/gencore.c, the SAME function the app and the host harness call -- so
     * tools/eval/parity_toolloop.py compares one implementation on two machines, id for id. The
     * turns cover each path: four answerable, one with an irrelevant value, a withheld value, no
     * values, and an explanation. Timed: time to the first generated token (prefill plus one step)
     * and to the end of the turn, the latency a student actually waits. */
    {
        static const struct { const char *kind, *f, *q; } TT[] = {
            { "answer", "I_S=((N_P)/(N_S))*I_P", "Using I_P = 8.54, N_P = 780, N_S = 2430, find I_S." },
            { "answer", "epsilon=B*l*v", "Take l = 0.057, v = 1.35, B = 0.196. What was the motionally induced emf?" },
            { "answer", "V=I*R", "Take R = 223.61, I = 74.54. Estimate V." },
            { "answer", "alpha=((Delta_omega)/(Delta_t))", "Where Delta_omega = 56.533, Delta_t = 689, compute angular acceleration." },
            { "extra value", "I=((P)/(A))", "Given m = 31.89, P = 559, A = 0.202, determine I." },
            { "value withheld", "f=((d_i*d_o)/(d_o+d_i))", "A system has v = 9700000, d_i = 39.6. Estimate f." },
            { "no values", "p=((F)/(A))", "determine pressure." },
            { "explain", "I=I_0*exp(-mu*x)", "How is I related to the other quantities?" },
        };
        for (unsigned ti = 0; ti < sizeof TT / sizeof TT[0]; ti++) {
            int ri = -1;
            for (int i = 0; i < st.n; i++)
                if (st.rec[i].formula && !strcmp(st.rec[i].formula, TT[ti].f)) { ri = i; break; }
            if (ri < 0) { say("turn %u: record %s NOT IN STORE -- skipped", ti, TT[ti].f); continue; }
            static ns_ask ask;
            ask_build(&st, TT[ti].q, &ask);
            if (ns_assemble(prompt, sizeof prompt, &st.rec[ri], ask.question, &ask.in) < 0) {
                say("turn %u: assemble overflow", ti); continue; }
            say("turn %u kind: %s", ti, TT[ti].kind);
            say("turn %u prompt: %s", ti, prompt);
            int tn = ns_tok_encode(&tk, prompt, ids, NS_MAX_TOKENS);
            if (tn <= 0) { say("turn %u: encode failed", ti); continue; }
            static tlm_gen_result R;
            TURN_FIRST = 0;
            uint32_t ta = MMIO32(T32 + T_VAL);
            TURN_T0 = ta;
            tlm_generate(&tk, ids, tn, &TURN_HOOKS, &R);
            uint32_t tb = MMIO32(T32 + T_VAL);
            static char tl[TLM_GEN_MAXEMIT * 7]; int q3 = 0;
            for (int i = 0; i < R.nemit && q3 < (int)sizeof tl - 8; i++)
                q3 += snprintf(tl + q3, sizeof tl - q3, "%d ", R.emitted[i]);
            say("turn %u ids: %s", ti, tl);
            static char tt[2048];
            ns_tok_decode(&tk, R.emitted, R.nemit, tt, sizeof tt);
            say("turn %u text: %s", ti, tt);
            uint32_t tf = TURN_FIRST ? (ta - TURN_TFIRST) : 0, tall = ta - tb;
            say("turn %u timing: prompt %d tokens, first output %u ticks = %u.%03u s, turn %u ticks = "
                "%u.%03u s, %d ids, %d calls", ti, tn, tf, tf / 32768u, (tf % 32768u) * 1000u / 32768u,
                tall, tall / 32768u, (tall % 32768u) * 1000u / 32768u, R.nemit, R.ncalls);
        }
    }
restore:
    MMIO32(T32 + T_CTRL) = 0;
    MMIO32(T32 + T_LOAD) = saved_load;
    MMIO32(T32 + T_CTRL) = saved_ctrl;   /* leave the timer as we found it -- the OS may own it */
    rq_free();
done:
    say("== done ==");
    if (LOG) fclose(LOG);
    screen_flush(); wait_key_pressed(); return 0;
}
