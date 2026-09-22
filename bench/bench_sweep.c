/* bench_sweep.c -- per-token cost across MODEL SHAPES, to give the frontier an x-axis.
 *
 * Every throughput number this project has is for one shape, d352 L6. A frontier needs a curve,
 * and training a checkpoint per point would cost days. It does not have to: decode cost on this
 * engine is a function of GEOMETRY, not of weight values -- fixed trip counts, no data-dependent
 * branch in the int8 kernel, nothing sparse or early-exited. So the throughput axis can be swept
 * with random-weight checkpoints (tools/make_shape.py) and only the QUALITY axis needs training.
 *
 * THAT ASSUMPTION IS NOT ARGUED, IT IS CONTROLLED. m352.bin.tns and m352b.bin.tns are the same
 * shape with different random weights. If weights mattered to timing, those two rows would
 * differ. They are the first and last entries so that any thermal drift across the run shows up
 * in the same comparison.
 *
 * THE CLIFF IS A DATA POINT, NOT A CRASH. src/nspire.c loads a checkpoint with ONE contiguous
 * malloc of the file size, so there is a shape above which nothing loads at all -- the
 * discontinuity the project brief calls the most interesting point in the sweep. rq_probe()
 * checks a candidate without allocating, and a malloc/free of the file size measures whether the
 * heap can actually hold it right now. Both run BEFORE rq_build(), because read_checkpoint()
 * exit()s on every failure path and would take the rest of the sweep with it.
 *
 * Battery only. bench_open prints the PLL-derived clock and marks a tethered run invalid.
 */
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void   rq_build(const char *path);
extern float *rq_forward(int token, int pos);
extern void   rq_free(void);
extern int    rq_probe(const char *path, char *why, int whylen);

/* DEFINED here, not extern. src/nspire.c and src/runq_nspire.c write their load traces through
 * this pointer guarded by `if (g_nspire_log)`, and every program that links them has to supply
 * it. Leaving it NULL is what once made bench_forward's log stop dead after the model path with
 * no way to tell an exit() from a hang. */
FILE *g_nspire_log = 0;

#define WARM 3
#define REPS 8

/* Smallest first, so a shape that cannot load does not cost the rows after it. The repeated
 * shape is deliberate -- see the header. */
static const char *SHAPES[] = {
    "m352.bin.tns",   /* control A: same shape, seed 1 */
    "m192.bin.tns",
    "m256.bin.tns",
    "m416.bin.tns",
    "m512.bin.tns",   /* expected ABOVE the single-malloc ceiling */
    "m352b.bin.tns",  /* control B: same shape, seed 99 */
};
#define NSHAPES ((int)(sizeof SHAPES / sizeof SHAPES[0]))

/* Positions timed per shape. Two points are enough to get the slope, and the project has already
 * learned what a single position costs: every earlier forward measurement sat at one position, so
 * the per-position term was unconstrained and two cost models were 54x and 4.7x wrong about it. */
static const int POS[] = { 8, 256 };
#define NPOS ((int)(sizeof POS / sizeof POS[0]))

static char PATHBUF[96];

int main(void) {
    bench_open("bench_sweep");
    g_nspire_log = bench_log();

    bench_result("note", "%s", "random weights: TIMING ONLY. These say nothing about quality "
                               "and must never be scored.");
    bench_result("header", "%s", "shape bytes params dim hidden L us_at_pos8 us_at_pos256 "
                                 "us_per_pos");

    for (int i = 0; i < NSHAPES; i++) {
        snprintf(PATHBUF, sizeof PATHBUF, "/documents/sweep/%s", SHAPES[i]);

        /* --- does the file exist, and how big is it --- */
        long fsz = 0;
        {   FILE *f = fopen(PATHBUF, "rb");
            if (!f) { bench_result("shape", "%s SKIPPED -- not on device", SHAPES[i]); continue; }
            fseek(f, 0, SEEK_END); fsz = ftell(f); fclose(f);
        }

        /* --- is it a checkpoint this engine accepts --- allocates nothing --- */
        {   char why[128] = { 0 };
            if (rq_probe(PATHBUF, why, sizeof why)) {
                bench_result("shape", "%s UNLOADABLE (%ld B): %s", SHAPES[i], fsz, why);
                continue;
            }
        }

        /* --- CAN THE HEAP HOLD IT, RIGHT NOW? ---
         * This is the cliff. The bare-probe heap figure this project measured on a fresh heap was
         * 4.5x larger than what the running program could get, so the question is asked HERE,
         * after the bench's own allocations, rather than taken from a table. */
        {   void *p = malloc((size_t)fsz);
            if (!p) {
                bench_result("shape", "%s DOES NOT FIT (%ld B): malloc of the file size failed "
                                      "in situ. THIS IS THE CEILING, measured.", SHAPES[i], fsz);
                continue;
            }
            free(p);
        }

        rq_build(PATHBUF);

        bench_timer_t tm;
        timer_acquire(&tm, TIMER_32K_BASE);
        uint32_t us[NPOS];
        for (int pi = 0; pi < NPOS; pi++) {
            for (int w = 0; w < WARM; w++) rq_forward(1, POS[pi] + w);
            uint32_t t0 = timer_raw(TIMER_32K_BASE);
            for (int r = 0; r < REPS; r++) rq_forward(1, POS[pi] + WARM + r);
            uint32_t ticks = timer_delta(t0, timer_raw(TIMER_32K_BASE)) / REPS;
            us[pi] = (uint32_t)((uint64_t)ticks * 1000000u / 32768u);
        }
        timer_release(&tm);

        /* Per-position slope from the two points, in us. Integer only: there is no FPU and a
         * soft-float divide in a reporting path runs at 3 MMAC/s against 48. */
        long slope = ((long)us[1] - (long)us[0]) / (POS[1] - POS[0]);
        bench_result("shape", "%s %ld B  pos%d=%lu us  pos%d=%lu us  per_pos=%ld us",
                     SHAPES[i], fsz, POS[0], (unsigned long)us[0],
                     POS[1], (unsigned long)us[1], slope);
        /* tok/s at each position, x1000, so the frontier can be read without post-processing. */
        bench_result("tok_s_x1000", "%s pos%d=%lu pos%d=%lu", SHAPES[i],
                     POS[0], (unsigned long)(us[0] ? 1000000000u / us[0] : 0),
                     POS[1], (unsigned long)(us[1] ? 1000000000u / us[1] : 0));

        rq_free();
    }

    bench_result("controls", "%s", "m352 and m352b are the SAME shape with different weights. "
                                   "If their rows differ, decode cost is not weight-independent "
                                   "and every other row in this table is suspect.");
    g_nspire_log = 0;
    bench_close();
    return 0;
}
