/* TOKEN-FOR-TOKEN host/device parity: the only parity claim worth making.
 *
 * docs/RESULT_HOST_DEVICE_PARITY.md established that this port is NOT bit-exact -- logits diverge
 * by up to 0.048 past position ~60 -- and then established that the gate asking for bit-exactness
 * was ill-posed, because two HOST builds differing only in -ffp-contract disagree with each other
 * by 0.098, more than the host and the device do. There is no unique host answer to match.
 *
 * What the system actually depends on is narrower and checkable: DOES THE CALCULATOR EMIT THE SAME
 * TOKENS. The eight-position golden showed argmax agreeing 8/8, but eight probes do not bound the
 * tail: a 0.048 logit deviation flips an argmax whenever the top two candidates are within 0.048,
 * and nothing measured how often that happens.
 *
 * So this decodes the SAME prompt the device decoded, greedily, and prints the ids for a direct
 * comparison against the ids in results/genlog_battery.txt.
 *
 * SCOPE, stated because it is narrower than "the device is correct": the prompt is the exact
 * string the device logged, hardcoded. This therefore tests TOKENISATION AND DECODING, not prompt
 * assembly -- ns_assemble's agreement with itself across host and device is checked separately by
 * gate_record_bytes, which diffs whole record spans against build/asmcli.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tokenizer.h"

#define main runq_main_unused
#include "../../src/runq_nspire.c"
#undef main

/* Verbatim from results/genlog_battery.txt, the line beginning "prompt:". */
static const char PROMPT[] =
    "<q>A car goes 150 m in 12 s. Find the speed. Given d = 150, t = 12.</q>"
    "<r>v=d/t | v:m/s d:m t:s | missing:none | standard conditions | fit:high";

/* The ids the CALCULATOR produced, battery, 396 MHz, same checkpoint. */
static const int DEVICE_IDS[] = { 5, 1969, 6, 364, 3292, 24, 26, 261, 376, 24, 26, 19, 7, 150, 0,
                                  30, 5, 302, 0, 4, 3765, 316, 225, 198 };
#define NDEV ((int)(sizeof DEVICE_IDS / sizeof DEVICE_IDS[0]))

static int argmax_of(const float *v, int n) {
    int best = 0;
    for (int i = 1; i < n; i++) if (v[i] > v[best]) best = i;
    return best;
}

int main(int argc, char **argv) {
    const char *model = argc > 1 ? argv[1] : "build/transfer/model4096.bin.tns";
    const char *tokp  = argc > 2 ? argv[2] : "build/tok4096.tok";

    static ns_tok tk;
    if (ns_tok_load(&tk, tokp) != NST_OK) { fprintf(stderr, "tokenizer: %s\n", tokp); return 2; }
    rq_build((char *)model);
    int V = rq_vocab();

    static int ids[1024];
    int n = ns_tok_encode(&tk, PROMPT, ids, 1024);
    printf("prompt tokens: %d   vocab %d\n", n, V);

    /* Prefill exactly as src/store/device_generate.c does: forward every prompt token but the
     * last, then decode from the last. An off-by-one here would make the comparison meaningless
     * while still producing plausible ids. */
    int tok = ids[0], pos = 0;
    while (pos < n - 1) { rq_forward(tok, pos); pos++; tok = ids[pos]; }

    int out[128], produced = 0;
    /* Decode to the device's own stop condition or 120 tokens, whichever comes first -- the same
     * rule as device_generate.c at GEN_MAX 120 -- so the host sequence is compared in full rather
     * than truncated to the 24-token device log that DEVICE_IDS preserves. */
    for (int s = 0; s < 120; s++) {
        float *lg = rq_forward(tok, pos);
        pos++;
        tok = argmax_of(lg, V);
        out[produced++] = tok;
        if (tok == 10) break;          /* the device's stop condition */
    }

    printf("host ids  :");
    for (int i = 0; i < produced; i++) printf(" %d", out[i]);
    printf("\ndevice ids:");
    for (int i = 0; i < NDEV; i++) printf(" %d", DEVICE_IDS[i]);
    printf("\n");

    int cmp = produced < NDEV ? produced : NDEV, agree = 0, first_diff = -1;
    for (int i = 0; i < cmp; i++) {
        if (out[i] == DEVICE_IDS[i]) agree++;
        else if (first_diff < 0) first_diff = i;
    }
    printf("\ncompared %d tokens: %d identical", cmp, agree);
    if (first_diff >= 0) printf(", FIRST DIVERGENCE at token %d (host %d, device %d)",
                                first_diff, out[first_diff], DEVICE_IDS[first_diff]);
    printf("\n");
    if (produced != NDEV) printf("NOTE: host produced %d tokens, device %d\n", produced, NDEV);

    static char txt[2048];
    ns_tok_decode(&tk, out, produced, txt, sizeof txt);
    printf("host text : %s\n", txt);
    return first_diff >= 0;
}
