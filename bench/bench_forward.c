/* bench_forward.c -- where does a token's time actually go, and what IS the fixed overhead?
 *
 * THE MEASUREMENT THAT DECIDES MODEL SIZE. docs/RESTRUCTURE_TABLES.md picks N from a two-anchor fit
 * with two free parameters: a MAC rate R and a fixed per-token overhead F0 ~= 180 ms. F0 was never
 * observed -- it is whatever was left over once R had also been fitted, it is 47% of the only good
 * decode measurement, and the whole parameter/context table rests on assuming it scales linearly in
 * L*dim. If it is actually flat, everything below 12M parameters gains 15-30% and the answer to
 * "how big can the model be" changes.
 *
 * Two independent things are measured here, and they cross-check each other:
 *
 *   1. THE INTERCEPT. Time per token is measured with 1, 2, 3, 4, 5 and 6 layers, on the SAME
 *      checkpoint -- the profile build can stop the layer loop early. The output is meaningless at
 *      L<6 and that is fine; this is a timing probe. Least squares on (L, time) gives a slope
 *      (cost per layer) and an INTERCEPT, and the intercept is F0 measured rather than inferred:
 *      everything that is not per-layer, which is the embedding lookup, the final norm and the
 *      classifier. No second checkpoint and no training run.
 *
 *   2. THE DECOMPOSITION. Every stage of a full forward pass is timed separately -- embedding,
 *      rmsnorm, quantize, QKV, RoPE, KV write, attention, softmax, FFN, classifier. The sum of the
 *      non-layer stages should equal the regression intercept. If those two disagree the model of
 *      the forward pass is wrong, and that disagreement is itself the finding.
 *
 * The classifier is the one to watch. At vocab 4096 it is dim*vocab = 1.18M MACs, 16% of the
 * weight MACs, and it runs ONCE per token rather than once per layer -- so it lands entirely in the
 * intercept. If F0 is mostly classifier, then F0 scales with vocab and dim but NOT with L, and the
 * table's linear-in-L*dim assumption is wrong in a way that favours deeper models.
 *
 * Timing: 32.768 kHz SP804, the crystal-derived cross-check bench_platform established. LOAD and
 * CONTROL are configured before any read -- reading this timer raw once produced a 2^32 underflow
 * in this project, and a stopped clock reports a rate rather than an error.
 *
 * RUN ON BATTERY, USB DISCONNECTED. Tethered is 288 MHz and every number here is void.
 */
#include "common.h"
#include "../src/store/loader.h"
#include <stdint.h>

/* runq_nspire.c and nspire.c log through this; the app defines it, a bench must too. */
FILE *g_nspire_log = 0;

extern void  rq_build(const char *path);
extern float *rq_forward(int token, int pos);
extern int   rq_vocab(void);
extern void  rq_free(void);

/* provided by the profile build of runq_nspire.c */
enum { PF_EMBED, PF_RMSNORM, PF_QUANT, PF_QKV, PF_ROPE, PF_KVWRITE,
       PF_ATTN, PF_SOFTMAX, PF_FFN, PF_CLS, PF_N };
extern uint32_t tlm_prof[PF_N];
extern int      tlm_prof_layers;

static const char *PFN[PF_N] = {
    "embedding", "rmsnorm", "quantize", "qkv matmul", "RoPE", "kv write",
    "attention", "softmax", "ffn matmul", "classifier"
};
/* Which stages are PER LAYER. The rest land in the intercept. */
static const int PER_LAYER[PF_N] = { 0, 1, 1, 1, 1, 1, 1, 1, 1, 0 };

#define WARM 3
#define REPS 12

static const char *DIRS[] = { "/documents/tlm/", "/documents/slm/", "/documents/ndless/" };
static char MODEL[80];

static int find_model(void) {
    for (unsigned i = 0; i < sizeof DIRS / sizeof DIRS[0]; i++) {
        snprintf(MODEL, sizeof MODEL, "%smodel4096.bin.tns", DIRS[i]);
        FILE *f = fopen(MODEL, "rb");
        if (f) { fclose(f); return 1; }
    }
    return 0;
}

int main(void) {
    bench_open("bench_forward");

    if (!find_model()) {
        bench_result("model", "%s", "NOT FOUND in /documents/{tlm,slm,ndless}/ -- nothing to time");
        bench_close();
        return 1;
    }
    bench_result("model", "%s", MODEL);
    rq_build(MODEL);
    int V = rq_vocab();
    bench_result("vocab", "%d", V);

    bench_timer_t tm;
    timer_acquire(&tm, TIMER_32K_BASE);

    /* ---- 1. the intercept, by varying L on one checkpoint ---------------------------------- */
    bench_result("probe", "%s", "time per token vs layer count, same weights");
    uint32_t per_L[8];
    int Lmax = 6;                       /* the ship config; find_model gives d288 L6 */
    for (int L = 1; L <= Lmax; L++) {
        tlm_prof_layers = L;
        for (int w = 0; w < WARM; w++) rq_forward(1, w);      /* warm caches, discard */
        uint32_t t0 = timer_raw(TIMER_32K_BASE);
        for (int r = 0; r < REPS; r++) rq_forward(1, WARM + r);
        uint32_t ticks = timer_delta(t0, timer_raw(TIMER_32K_BASE));
        per_L[L] = ticks / REPS;
        bench_result("t_per_token_L", "%d layers = %lu ticks = %lu us",
                     L, (unsigned long)per_L[L],
                     (unsigned long)((uint64_t)per_L[L] * 1000000u / 32768u));
    }

    /* least squares on (L, ticks). Integer arithmetic: no FPU, and a soft-float divide in a
     * reporting path is a needless 3 MMAC/s detour. */
    {
        long n = Lmax, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int L = 1; L <= Lmax; L++) { sx += L; sy += per_L[L]; sxx += L*L; sxy += (long)L*per_L[L]; }
        long den = n*sxx - sx*sx;
        long slope_num = n*sxy - sx*sy;              /* ticks per layer, x1000 below */
        long icept_num = sy*sxx - sx*sxy;
        long slope_m = den ? (slope_num * 1000 / den) : 0;
        long icept_m = den ? (icept_num * 1000 / den) : 0;
        bench_result("per_layer_ticks", "%ld.%03ld", slope_m/1000, (slope_m%1000+1000)%1000);
        bench_result("per_layer_us", "%ld", slope_m*1000000/32768/1000);
        bench_result("INTERCEPT_ticks", "%ld.%03ld", icept_m/1000, (icept_m%1000+1000)%1000);
        bench_result("INTERCEPT_us", "%ld", icept_m*1000000/32768/1000);
        bench_result("INTERCEPT_note", "%s",
            "this is F0 MEASURED. The table assumed ~180000 us at d288 L6.");
    }

    /* ---- 2. the decomposition of one full pass --------------------------------------------- */
    tlm_prof_layers = -1;                                   /* all layers */
    for (int i = 0; i < PF_N; i++) tlm_prof[i] = 0;
    for (int w = 0; w < WARM; w++) rq_forward(1, w);
    for (int i = 0; i < PF_N; i++) tlm_prof[i] = 0;         /* discard warm-up */
    uint32_t t0 = timer_raw(TIMER_32K_BASE);
    for (int r = 0; r < REPS; r++) rq_forward(1, WARM + r);
    uint32_t total = timer_delta(t0, timer_raw(TIMER_32K_BASE)) / REPS;

    uint32_t sum = 0, per_layer_sum = 0, fixed_sum = 0;
    for (int i = 0; i < PF_N; i++) {
        uint32_t v = tlm_prof[i] / REPS;
        sum += v;
        if (PER_LAYER[i]) per_layer_sum += v; else fixed_sum += v;
        bench_result("stage", "%-11s %6lu ticks  %6lu us  %3lu%%  %s",
                     PFN[i], (unsigned long)v,
                     (unsigned long)((uint64_t)v * 1000000u / 32768u),
                     (unsigned long)(total ? (uint64_t)v * 100u / total : 0),
                     PER_LAYER[i] ? "per-layer" : "FIXED");
    }
    bench_result("stage_sum",  "%lu ticks (%lu us)", (unsigned long)sum,
                 (unsigned long)((uint64_t)sum * 1000000u / 32768u));
    bench_result("wall_total", "%lu ticks (%lu us)", (unsigned long)total,
                 (unsigned long)((uint64_t)total * 1000000u / 32768u));
    bench_result("unattributed", "%ld ticks -- loop overhead and anything not instrumented",
                 (long)total - (long)sum);
    bench_result("FIXED_stages_us", "%lu -- embedding + final norm + classifier",
                 (unsigned long)((uint64_t)fixed_sum * 1000000u / 32768u));
    bench_result("CROSS_CHECK", "%s",
        "FIXED_stages_us should equal INTERCEPT_us. A disagreement means the model of the "
        "forward pass is wrong, and that is the finding.");

    timer_release(&tm);
    rq_free();
    bench_close();
    return 0;
}
