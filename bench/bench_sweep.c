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
/* EVERY DIM HERE IS A MULTIPLE OF 88, and that is a constraint, not a preference.
 *
 * FIXED_GS is 88 in src/runq_nspire.c, compile-time, so the hot loop loses two __divsi3 per
 * group. A checkpoint quantised at any other group is refused at load. The first version of this
 * sweep used d192/d256/d416, whose tensor lengths take groups 96/64/64, and THREE OF FIVE SHAPES
 * WERE UNLOADABLE -- "GS 96, built for 88". dim*dim must divide 88 = 2^3 * 11 (so dim divisible
 * by 44) and heads need dim divisible by 8, which leaves multiples of 88.
 *
 * No shape above the ceiling is listed. heap_ceiling() measures that directly and to the
 * kilobyte; shipping a 24 MB checkpoint to watch it fail was both indirect and, as it turned out,
 * untransferable -- the push stalled at 7.5 minutes having used 0.11 s of CPU. */
static const char *SHAPES[] = {
    "m352.bin.tns",   /* control A: same shape, seed 1 */
    "m176.bin.tns",
    "m264.bin.tns",   /* 8 heads, head_size 33: the engine must now REFUSE it (A143) */
    "m264h6.bin.tns", /* 6 heads, head_size 44: the trainable d264 -- replaces the row above */
    "m440.bin.tns",
    "m352b.bin.tns",  /* control B: same shape, seed 99 */
};
#define NSHAPES ((int)(sizeof SHAPES / sizeof SHAPES[0]))

/* Positions timed per shape. Two points are enough to get the slope, and the project has already
 * learned what a single position costs: every earlier forward measurement sat at one position, so
 * the per-position term was unconstrained and two cost models were 54x and 4.7x wrong about it. */
static const int POS[] = { 8, 256 };
#define NPOS ((int)(sizeof POS / sizeof POS[0]))

static char PATHBUF[96];

/* THE CEILING, MEASURED DIRECTLY RATHER THAN INFERRED FROM A FILE THAT WILL NOT LOAD.
 *
 * src/nspire.c loads a checkpoint with ONE contiguous malloc of the file size, so the binding
 * constraint on model size is the largest single allocation, not total free heap. Finding it by
 * shipping an over-sized checkpoint and watching it fail is indirect, needs a 24 MB transfer, and
 * answers only "bigger than this one". A binary search over malloc answers it to the kilobyte.
 *
 * It is called TWICE, and the difference between the two calls is the finding. This project once
 * recorded 21.56 MiB from a probe on a fresh heap and later measured 4.83 MiB in a program that
 * had done ordinary work first -- a factor of 4.5, and the fresh-heap number was the denominator
 * of every capacity claim in the repo. A ceiling is a property of a moment, not of the device.
 */
static size_t heap_ceiling(void) {
    size_t lo = 0, hi = 48u << 20;          /* 48 MB is comfortably past any plausible answer */
    while (hi - lo > 4096) {
        size_t mid = lo + (hi - lo) / 2;
        void *p = malloc(mid);
        if (p) { free(p); lo = mid; } else { hi = mid; }
    }
    return lo;
}

int main(void) {
    bench_open("bench_sweep");
    g_nspire_log = bench_log();

    {   size_t c = heap_ceiling();
        bench_result("heap_ceiling_fresh", "%lu B = %lu.%02lu MiB (largest single malloc, before "
                     "any model is loaded)", (unsigned long)c,
                     (unsigned long)(c >> 20), (unsigned long)(((c & 0xFFFFF) * 100) >> 20));

        /* THE HEAP DOES NOT COME BACK AFTER A CRASH, AND A SWEEP ON A DEPLETED HEAP LOOKS LIKE A
         * RESULT. Measured: 21.64 MiB on one run and 5.02 MiB on the next, same program, same
         * device -- because the run in between died inside read_checkpoint, which exit()s, and
         * its ~19 MB was never returned. On the depleted run EVERY shape reported DOES NOT FIT,
         * which reads as a finding about model size and is a finding about the previous crash.
         *
         * So the operator is told, in the log, at the top, before any row that would mislead. */
        if (c < (16u << 20))
            bench_result("HEAP DEPLETED", "only %lu.%02lu MiB available. A fresh boot gives over "
                         "21 MiB. Something -- most likely a program that exit()ed mid-load -- is "
                         "still holding memory. REBOOT THE CALCULATOR AND RUN THIS AGAIN; the "
                         "rows below are about this heap, not about these models.",
                         (unsigned long)(c >> 20), (unsigned long)(((c & 0xFFFFF) * 100) >> 20));
    }

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

        /* --- CAN THE HEAP HOLD THE FILE **AND** THE KV CACHE, RIGHT NOW? ---
         *
         * THE BINDING CONSTRAINT IS NOT THE SINGLE-MALLOC CEILING. This repo's standing note says
         * "the binding ceiling is B1, the largest single allocation, not B2 total heap", because
         * the checkpoint is loaded with one contiguous malloc. That is true of the checkpoint and
         * false of the program: malloc_run_state then asks for an fp32 KV cache of
         * 2 * n_layers * seq_len * kv_dim * 4 bytes, which at d440 is 10.3 MiB on top of a
         * 16.6 MiB file. The sum is what binds, and the KV term grows with dim AND with seq_len.
         *
         * Measured the hard way: the first run of this sweep DIED on d440 with a truncated log
         * and no error line, because rq_build's failure path exit()s. Checking a single malloc of
         * the file size was not enough -- it passed, and the program died anyway.
         *
         * Both blocks are held at once, because holding them one at a time answers a question
         * nobody asked. */
        {   unsigned char h[64];
            FILE *f = fopen(PATHBUF, "rb");
            if (!f || fread(h, 1, sizeof h, f) != sizeof h) {
                if (f) fclose(f);
                bench_result("shape", "%s UNREADABLE header", SHAPES[i]);
                continue;
            }
            fclose(f);
            int dim, hid, nl, nh, nkv, voc, seq;
            memcpy(&dim, h + 8, 4);  memcpy(&hid, h + 12, 4); memcpy(&nl, h + 16, 4);
            memcpy(&nh, h + 20, 4);  memcpy(&nkv, h + 24, 4); memcpy(&voc, h + 28, 4);
            memcpy(&seq, h + 32, 4);
            size_t kv_dim = (size_t)dim * nkv / nh;
            size_t kv_bytes = 2u * (size_t)nl * (size_t)seq * kv_dim * sizeof(float);

            void *a = malloc((size_t)fsz);
            void *b = a ? malloc(kv_bytes) : NULL;
            int fits = a && b;
            free(b); free(a);
            bench_result("need", "%s dim=%d hidden=%d L=%d seq=%d file=%ld B kv=%lu B total=%lu B %s",
                         SHAPES[i], dim, hid, nl, seq, fsz, (unsigned long)kv_bytes,
                         (unsigned long)((size_t)fsz + kv_bytes), fits ? "FITS" : "DOES NOT FIT");
            if (!fits) {
                bench_result("shape", "%s SKIPPED -- file + KV cache do not fit in situ. "
                                      "THIS IS THE CEILING, and it is the SUM, not the file.",
                             SHAPES[i]);
                continue;
            }
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

        /* THE CEILING AFTER EVERY SHAPE, so a leak is a measurement instead of a silence.
         *
         * The first version of this sweep died on its fourth load with a truncated log and no
         * error line -- free_run_state was leaking the int8 KV cache on every build/free cycle
         * (A139), and nothing printed said so. A ceiling that walks downward across rows names
         * that immediately; a flat one is evidence the engine really does give the memory back. */
        {   size_t c = heap_ceiling();
            bench_result("heap_after", "%s -> %lu B = %lu.%02lu MiB", SHAPES[i],
                         (unsigned long)c, (unsigned long)(c >> 20),
                         (unsigned long)(((c & 0xFFFFF) * 100) >> 20)); }
    }

    /* The same question again, now that the sweep has loaded and freed several checkpoints. If
     * this is far below the fresh figure, the ceiling that matters is this one -- and every
     * capacity claim taken from a fresh-heap probe is an overestimate. */
    {   size_t c = heap_ceiling();
        bench_result("heap_ceiling_after", "%lu B = %lu.%02lu MiB (largest single malloc AFTER "
                     "the sweep; compare with heap_ceiling_fresh)", (unsigned long)c,
                     (unsigned long)(c >> 20), (unsigned long)(((c & 0xFFFFF) * 100) >> 20)); }

    bench_result("controls", "%s", "m352 and m352b are the SAME shape with different weights. "
                                   "If their rows differ, decode cost is not weight-independent "
                                   "and every other row in this table is suspect.");
    g_nspire_log = 0;
    bench_close();
    return 0;
}
