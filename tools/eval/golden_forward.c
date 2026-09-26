/* THE FORWARD-PASS CORRECTNESS ORACLE, which did not exist.
 *
 * test_ckpt validates the LOADER -- magic, version, Config, truncation. Nothing validated the
 * NUMERICS of rq_forward, so a change to the hot loop had no way to be checked except by running
 * the whole app and looking at it. Phase 1 of the brief says this artefact should exist: "capture
 * golden token sequences and per layer activation checksums. These become the correctness oracle
 * for every device build."
 *
 * It compiles runq_nspire.c on the HOST, which is the same source the calculator runs, so a
 * bit-exactness claim about a hot-loop change is checkable without a device round-trip.
 *
 * WHAT IT EMITS: for a fixed (token, position) walk, an FNV-1a hash of the full logit vector after
 * every step, plus the first and last few logits verbatim. A hash alone would tell us THAT
 * something moved and not by how much; the raw values make a near-miss legible.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef __arm__
#include "nspire_screen.h"
#endif
#define main runq_main_unused
#include "../../src/runq_nspire.c"
#undef main

static uint64_t fnv1a(const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}

int main(int argc, char **argv) {
#ifdef __arm__
    /* WITHOUT THIS THE PROGRAM IS SILENT AND LOOKS LIKE A CRASH. Every working bench calls
     * screen_init() through bench_open() before its first printf; this file did not, so on device
     * it flashed and exited with nothing on screen and nothing written. Three of the five Phase 2
     * bring-up bugs were exactly this class -- an unrouted console, indistinguishable from a hang.
     * The repo's own rule: prove the diagnostic can print from the translation unit under test. */
    screen_init();
#endif
    /* The Ndless launcher gives no useful argv, so the device needs its own default. The path is
     * the one src/store/device_app.c's DATA_DIRS lists first. On the host the build-tree path is
     * still the default, so `make golden_forward` is unchanged. */
#ifdef __arm__
    const char *model = argc > 1 ? argv[1] : "/documents/tlm/model4096.bin.tns";
#else
    const char *model = argc > 1 ? argv[1] : "build/transfer/model4096.bin.tns";
#endif
    rq_build((char *)model);
    int V = rq_vocab();
    int SL = rq_seq_len();
    printf("model=%s\nvocab=%d seq_len=%d\n", model, V, SL);

    /* A deterministic walk: token ids that exist in any 4096 vocab, positions that exercise the
     * attention loop at short AND long context -- the loop under change is O(pos), so a golden
     * taken only at pos 0 would not cover it. */
    const int toks[] = { 1, 2, 100, 7, 4095 % 4096, 42, 3, 900 };
    const int NT = (int)(sizeof toks / sizeof toks[0]);
    uint64_t chain = 1469598103934665603ULL;
    uint64_t step_hash[NT];
    /* A HASH ONLY SAYS DIFFERENT. The device chain diverged from the host at step 0, and FNV over
     * raw float bytes flips completely on one ULP of soft-float rounding -- so the hash cannot
     * distinguish "ARM rounds differently" from "the matmul is wrong". These carry the magnitude:
     * three sampled logits, and the argmax, which is what actually decides a token. */
    int step_argmax[NT];
    float step_l0[NT], step_l1[NT], step_lN[NT];
    int prev_pos = 0;
    for (int step = 0; step < NT; step++) {
        int pos = step * 31;                 /* 0, 31, 62, ... spans the attention range */
        /* FILL THE INTERVENING POSITIONS. The walk used to JUMP, leaving the KV cache zeroed at
         * every position it skipped -- at pos=217 that is 210 of 218 slots holding zero keys and
         * values, so the softmax spread most of its mass across zero-vectors. That is the worst
         * case for accumulation-order differences and it is nothing like generation, which writes
         * every position. It showed up exactly there: host-vs-device agreed to ~1e-6 at steps 0-6
         * and jumped to ~1e-2 at step 7, four orders, discontinuously.
         *
         * Real generation fills the cache. So does this now: every position up to the sample point
         * is computed, and only the sample points are hashed. The cost is 218 forwards instead of
         * 8, which is minutes on device and worth it -- a divergence measured on a cache that is
         * 96% zeroes is a measurement of the instrument. */
        for (int fill = prev_pos; fill < pos; fill++)
            (void)rq_forward(toks[step] % V, fill);
        if (pos >= SL) pos = SL - 1;
        float *logits = rq_forward(toks[step] % V, pos);
        uint64_t h = fnv1a(logits, (size_t)V * sizeof(float));
        step_hash[step] = h;
        { int am = 0; for (int v = 1; v < V; v++) if (logits[v] > logits[am]) am = v;
          step_argmax[step] = am; }
        step_l0[step] = logits[0]; step_l1[step] = logits[1]; step_lN[step] = logits[V - 1];
        prev_pos = pos + 1;
        chain ^= h; chain *= 1099511628211ULL;
        printf("step=%d tok=%d pos=%d logit_hash=%016llx argmax=%d l0=%.9g l1=%.9g lN=%.9g\n",
               step, toks[step] % V, pos, (unsigned long long)h, step_argmax[step],
               logits[0], logits[1], logits[V - 1]);
    }
    printf("CHAIN=%016llx\n", (unsigned long long)chain);

#ifdef __arm__
    /* A RESULT THAT CANNOT BE TRANSFERRED IS A RESULT YOU DO NOT HAVE. The screen shows a 16-hex
     * chain hash that would otherwise be copied by hand off a calculator LCD, which is exactly the
     * transcription this project has already lost a CAS timing to.
     *
     * Its OWN small file, not an append to the bench results log: that log reached 114 KB and
     * then failed to pull six consecutive times with "Invalid packet received" while a LARGER
     * file pulled cleanly, so the fault was the file. A few hundred bytes, written fresh each run.
     *
     * A140. IN /documents/tlm/, NOT /documents/bench/. push-all.sh created /bench unconditionally
     * until that was made conditional on PUSH_BENCH; the directory then stopped existing, fopen
     * returned NULL, and a real device run of this oracle produced nothing but a screen-only
     * chain hash -- a 16-hex-digit bit-exactness verdict that could only be recovered by reading
     * it off the display and retyping it, where one wrong nibble inverts the conclusion.
     *
     * THIRD INSTANCE of this exact defect: bench/common.h wrote there, this wrote there, and both
     * were silently unwritable. /documents/tlm/ holds the model, so any device that can run this
     * at all has it. The missing abstraction is a single "where do device results go" helper;
     * that is recorded as debt rather than resolved here. */
    {
        FILE *g = fopen("/documents/tlm/golden_dev.txt.tns", "w");
        if (g) {
            fprintf(g, "model=%s\nvocab=%d seq_len=%d GS=%d\n", model, V, SL, FIXED_GS);
            for (int step = 0; step < NT; step++) {
                int pos = step * 31; if (pos >= SL) pos = SL - 1;
                fprintf(g, "step=%d pos=%d hash=%016llx argmax=%d l0=%.9g l1=%.9g lN=%.9g\n",
                        step, pos, (unsigned long long)step_hash[step], step_argmax[step],
                        (double)step_l0[step], (double)step_l1[step], (double)step_lN[step]);
            }
            fprintf(g, "CHAIN=%016llx\n", (unsigned long long)chain);
            fclose(g);
            /* THE WHOLE STEP-0 VECTOR, so the divergence can be measured rather than described.
             * 4096 floats is 16 KB -- small enough to pull reliably, unlike the 114 KB CAS log
             * that failed six consecutive transfers. */
            FILE *b = fopen("/documents/tlm/logits0.bin.tns", "wb");
            if (b) {
                float *l0v = rq_forward(toks[0] % V, 0);
                fwrite(l0v, sizeof(float), (size_t)V, b);
                fclose(b);
                printf("wrote /documents/tlm/logits0.bin.tns\n");
            }
            printf("wrote /documents/tlm/golden_dev.txt.tns\n");
        } else {
            printf("WARNING: could not write the result file; the CHAIN above is screen-only\n");
        }
    }
#endif
    /* Optional: dump every logit so the two KV arms can be compared numerically. A CHAIN hash only
     * says DIFFERENT; the acceptance criteria are about HOW different. */
    if (argc > 2) {
        FILE *o = fopen(argv[2], "wb");
        if (!o) { fprintf(stderr, "cannot write %s\n", argv[2]); return 2; }
        for (int step = 0; step < NT; step++) {
            int pos = step * 31; if (pos >= SL) pos = SL - 1;
            float *lg = rq_forward(toks[step] % V, pos);
            fwrite(lg, sizeof(float), (size_t)V, o);
        }
        fclose(o);
    }
    /* FREE WHAT WAS BUILT. On the calculator this program runs under Ndless, which does not reclaim
     * the heap from a program that exits holding it: golden_dev left ~20 MB allocated, and ChatTLM,
     * opened next, reported "Not enough free RAM" for its 11.6 MB model block. Every other device
     * benchmark already called rq_free(); this was the one that did not. */
    rq_free();
    return 0;
}
