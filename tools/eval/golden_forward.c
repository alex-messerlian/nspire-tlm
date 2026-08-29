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
    for (int step = 0; step < NT; step++) {
        int pos = step * 31;                 /* 0, 31, 62, ... spans the attention range */
        if (pos >= SL) pos = SL - 1;
        float *logits = rq_forward(toks[step] % V, pos);
        uint64_t h = fnv1a(logits, (size_t)V * sizeof(float));
        chain ^= h; chain *= 1099511628211ULL;
        printf("step=%d tok=%d pos=%d logit_hash=%016llx  l0=%.9g l1=%.9g lN=%.9g\n",
               step, toks[step] % V, pos, (unsigned long long)h,
               logits[0], logits[1], logits[V - 1]);
    }
    printf("CHAIN=%016llx\n", (unsigned long long)chain);
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
    return 0;
}
