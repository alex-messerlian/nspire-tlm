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
    /* A146. PARITY OVER MORE THAN ONE PROMPT. Every parity result so far is one prompt, and the
     * model stops itself at 33 tokens there, so raising the token cap could not widen the sample.
     * More prompts can. Each prompt is logged VERBATIM so the host (tools/eval/golden_decode.c)
     * replays exactly the string the calculator tokenised -- the host never re-assembles it, so a
     * prompt-assembly difference cannot hide inside a decoding comparison. Untimed. */
    {
        static const struct { const char *f, *q; const char *nm[3], *vl[3]; int nv; } PP[] = {
            { "F=m*a",  "A 3 kg cart accelerates at 4 m/s^2. Find the net force.",        {"m","a"}, {"3","4"},    2 },
            { "W=F*d",  "A 20 N force pushes a box 5 m. How much work is done?",          {"F","d"}, {"20","5"},   2 },
            { "p=m*v",  "A 0.5 kg ball moves at 12 m/s. Find its momentum.",              {"m","v"}, {"0.5","12"}, 2 },
            { "V=I*R",  "A current of 2 A flows through a 6 ohm resistor. Find the voltage.", {"I","R"}, {"2","6"}, 2 },
            { "F=-k*x", "A spring with k = 50 N/m is stretched 0.2 m. Find the force.",   {"k","x"}, {"50","0.2"}, 2 },
        };
        for (unsigned pi = 0; pi < sizeof PP / sizeof PP[0]; pi++) {
            int ri = -1;
            for (int i = 0; i < st.n; i++) if (!strcmp(st.rec[i].formula, PP[pi].f)) { ri = i; break; }
            if (ri < 0) { say("parity %u: record %s NOT IN STORE -- skipped", pi, PP[pi].f); continue; }
            ns_input pin; memset(&pin, 0, sizeof pin);
            for (int k = 0; k < PP[pi].nv; k++) { pin.var[k] = PP[pi].nm[k]; pin.val[k] = PP[pi].vl[k]; }
            pin.nvals = PP[pi].nv;
            ns_assemble(prompt, sizeof prompt, &st.rec[ri], PP[pi].q, &pin);
            say("parity %u prompt: %s", pi, prompt);
            int pn = ns_tok_encode(&tk, prompt, ids, NS_MAX_TOKENS);
            if (pn <= 0) { say("parity %u: encode failed", pi); continue; }
            int pt = ids[0], pp = 0, pc = 0;
            while (pp < pn - 1) { rq_forward(pt, pp); pp++; pt = ids[pp]; }
            for (int s3 = 0; s3 < GEN_MAX; s3++) {
                float *lg = rq_forward(pt, pp); pp++;
                pt = argmax(lg, V); out[pc++] = pt;
                if (pt == 10) break;
            }
            static char pl[GEN_MAX * 7]; int q2 = 0;
            for (int i = 0; i < pc && q2 < (int)sizeof pl - 8; i++) q2 += snprintf(pl + q2, sizeof pl - q2, "%d ", out[i]);
            say("parity %u ids: %s", pi, pl);
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
