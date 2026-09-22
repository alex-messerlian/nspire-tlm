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
    LOG = fopen("/documents/tlm/genlog.txt.tns", "w");
    g_nspire_log = LOG;
    say("== generate ==");
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
    static int out[64];
    uint32_t t0 = MMIO32(T32 + T_VAL);
    while (pos < n - 1) { rq_forward(tok, pos); pos++; tok = ids[pos]; }
    uint32_t t1 = MMIO32(T32 + T_VAL);
    for (int s2 = 0; s2 < 24; s2++) {
        float *lg = rq_forward(tok, pos);
        pos++;
        tok = argmax(lg, V);
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
    { char line[300]; int p2 = 0;
      for (int i = 0; i < produced && p2 < 270; i++)
          p2 += snprintf(line + p2, sizeof line - p2, "%d ", out[i]);
      say("ids: %s", line); }
    { static char txt[1024];
      ns_tok_decode(&tk, out, produced, txt, sizeof txt);
      say("TEXT: %s", txt); }
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
