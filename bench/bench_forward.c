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

/* runq_nspire.c and nspire.c log through this; the app defines it, a bench must too.
 *
 * ASSIGNED IN main(), not left at 0. Leaving it NULL is what cost three device passes: every
 * failure path from rq_build() reports through fprintf(stderr,...) -- routed to the SCREEN by
 * nspire_screen.h and to nowhere else -- and every load STEP trace in nspire.c is guarded by
 * `if (g_nspire_log)`. The operator pulls the log, not the screen. So the log recorded the header,
 * the model path, and then nothing at all, three sessions running, which is indistinguishable from
 * a hang and told us nothing about which of half a dozen exit() paths had fired. */
FILE *g_nspire_log = 0;

extern void  rq_build(const char *path);
extern int   rq_probe(const char *path, char *why, int cap);
extern float *rq_forward(int token, int pos);
extern int   rq_vocab(void);
extern void  rq_free(void);

/* provided by the profile build of runq_nspire.c. Matched to that enum BY ORDINAL -- if you add a
 * slot there, add it here, in the same position. */
enum { PF_EMBED, PF_RMSNORM, PF_QUANT, PF_QKV, PF_ROPE, PF_KVWRITE,
       PF_ATTN, PF_SOFTMAX, PF_FFN, PF_CLS, PF_FNORM, PF_FQUANT, PF_N };
extern uint32_t tlm_prof[PF_N];
extern int      tlm_prof_layers;

static const char *PFN[PF_N] = {
    "embedding", "rmsnorm", "quantize", "qkv matmul", "RoPE", "kv write",
    "attention", "softmax", "ffn matmul", "classifier", "final norm", "final quant"
};
/* Which stages are PER LAYER. The rest land in the intercept.
 * `final norm` and `final quant` are 0: they run once per token, and counting them as per-layer is
 * what used to make FIXED_stages_us short by two stages and the CROSS_CHECK fail by construction. */
static const int PER_LAYER[PF_N] = { 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0 };

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

    /* ---- 0. PROBE BEFORE BUILDING. -------------------------------------------------------------
     *
     * rq_build() reaches read_checkpoint(), which exit()s on EVERY failure path -- bad magic, wrong
     * version, GS mismatch, size mismatch, failed map -- and reports each one through
     * fprintf(stderr,...). Under Ndless that reaches the screen at best. A process that vanishes
     * after printing the model path is indistinguishable from a hang, and that ambiguity is exactly
     * what has held this measurement for three device passes.
     *
     * rq_probe() exists for this and allocates nothing. src/store/device_app.c has called it at
     * startup since the app was written; this bench was the one caller that walked straight into
     * the exit() path with no instrument attached. */
    g_nspire_log = bench_log();          /* BEFORE any engine call, so its traces reach the file */
    bench_result("log_routed", "%s", g_nspire_log ? "yes -- engine traces will appear below"
                                                  : "NO -- bench log not open, engine will be silent");

    {   char why[128] = { 0 };
        int bad = rq_probe(MODEL, why, sizeof why);
        bench_result("probe", "%s", bad ? why : "ok");
        if (bad) {
            /* "Cannot measure" and "measured" must never share an output. */
            bench_result("RESULT", "%s", "ABORTED -- the checkpoint is unloadable, see probe= above. "
                                         "No timing was taken. This is not a slow run or a hang.");
            g_nspire_log = 0;
            bench_close();
            return 1;
        }
    }

    bench_result("stage", "%s", "rq_build: entering read_checkpoint + malloc_run_state");
    rq_build(MODEL);
    bench_result("stage", "%s", "rq_build: returned");
    int V = rq_vocab();
    bench_result("vocab", "%d", V);

    bench_timer_t tm;
    timer_acquire(&tm, TIMER_32K_BASE);

    /* ---- 1. the intercept, by varying L on one checkpoint ---------------------------------- */
    /* Key renamed from "probe": `probe=` is now the checkpoint probe's verdict, and two different
     * facts sharing a log key is how a grep for one silently returns the other. */
    bench_result("part1", "%s", "time per token vs layer count, same weights");
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
        /* 64-BIT, BECAUSE slope_m AND icept_m ARE MILLI-TICKS AND long IS 32 BITS HERE.
         *
         * These two lines were the only conversions in this file that did not widen, and both
         * overflowed: 1412485 milli-ticks * 1000000 = 1.41e12, which wraps mod 2^32 to a NEGATIVE
         * int32. The device printed per_layer_us=-17 and INTERCEPT_us=-28 -- and NEGATIVE
         * MICROSECONDS FROM POSITIVE TICK COUNTS IS ARITHMETIC, NOT PHYSICS. Reproducing the wrap
         * off-device gives exactly -17 and -29 against the printed -17 and -28.
         *
         * The damage was not the wrong number, it was the wrong CONCLUSION: CROSS_CHECK below
         * compares INTERCEPT_us to FIXED_stages_us and says a disagreement means the model of the
         * forward pass is wrong. It read as a catastrophic failure of that model. With the widen,
         * F0 = 39,555 us against 39,154 us measured directly -- 1.0% apart, and the check PASSES.
         * A reporting-path overflow nearly cost us the headline result of the pass. */
        bench_result("per_layer_ticks", "%ld.%03ld", slope_m/1000, (slope_m%1000+1000)%1000);
        bench_result("per_layer_us", "%lld", (long long)((int64_t)slope_m * 1000000 / 32768 / 1000));
        bench_result("INTERCEPT_ticks", "%ld.%03ld", icept_m/1000, (icept_m%1000+1000)%1000);
        bench_result("INTERCEPT_us", "%lld", (long long)((int64_t)icept_m * 1000000 / 32768 / 1000));
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
    bench_result("FIXED_stages_us", "%lu -- embedding + final norm + final quant + classifier",
                 (unsigned long)((uint64_t)fixed_sum * 1000000u / 32768u));
    /* per_layer_sum was computed and never reported -- a second cross-check, already paid for.
     * The regression's SLOPE is cost per layer; this sum divided by n_layers is the same quantity
     * measured a different way. They agree only if per-layer cost is actually uniform across
     * layers, which is the "linear in L" assumption RESTRUCTURE_TABLES.md's whole parameter table
     * rests on and which nothing has ever tested. */
    bench_result("PERLAYER_stages_us", "%lu total over %d layers = %lu us/layer",
                 (unsigned long)((uint64_t)per_layer_sum * 1000000u / 32768u), Lmax,
                 (unsigned long)((uint64_t)per_layer_sum * 1000000u / 32768u / (unsigned)Lmax));
    bench_result("CROSS_CHECK_1", "%s",
        "FIXED_stages_us should equal INTERCEPT_us. A disagreement means the model of the "
        "forward pass is wrong, and that is the finding.");
    bench_result("CROSS_CHECK_2", "%s",
        "PERLAYER_stages_us/layer should equal per_layer_us from the regression. A disagreement "
        "means per-layer cost is NOT uniform in L, which is the table's load-bearing assumption.");

    timer_release(&tm);
    rq_free();
    /* Drop the engine's handle BEFORE bench_close() fcloses it; a dangling FILE* here would be a
     * use-after-free on the next engine log line, on a device with no fault handler. */
    g_nspire_log = 0;
    bench_close();
    return 0;
}
