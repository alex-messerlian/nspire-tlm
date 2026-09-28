/* bench_mac.c: fills HARDWARE.md rows B10-B12.
 *
 * This benchmark exists to settle the project's central disagreement. The brief asserts the device
 * is memory-bandwidth bound. ARM926EJ-S is a single-issue, no-SIMD core that retires at most one
 * MAC per cycle, which is unusually weak compute next to even a mediocre DRAM. If B11 (MAC rate)
 * turns out low relative to B4 (bandwidth), the machine is COMPUTE bound at int4 and the brief's
 * optimization priority order is wrong. See README.md section "Pushback".
 *
 * NOT YET COMPILED.
 */
#include "common.h"

#define VEC_N     4096u        /* one matvec row: 4096 elements */
#define ROWS      2048u        /* 8.4M MACs per pass -- big enough to swamp timer overhead */

/* ---- B10: naive int8 dot product, the llama2.c-shaped baseline ------------------------------ */
static int32_t dot_int8_naive(const int8_t *w, const int8_t *x, uint32_t n) {
    int32_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc += (int32_t)w[i] * (int32_t)x[i];
    return acc;
}

/* ---- B11: int16 MAC via ARMv5TE SMLABB ------------------------------------------------------ */
/* SMLABB rd, rn, rm, ra: rd = (int16)rn * (int16)rm + ra, one cycle issue on ARM926EJ-S.
 * SMLATT uses the TOP halves of both operands, so one 32-bit load feeds two MACs. That halves the
 * load count, which is the actual bottleneck -- the MAC itself was never the problem.
 *
 * ARMv5TE has NO SMLAD (dual 16x16 MAC) -- that is ARMv6. So 1 MAC/instruction is the ceiling here,
 * and the only lever is reducing everything AROUND the MAC. */
static int32_t dot_int16_smla(const int16_t *w, const int16_t *x, uint32_t n) {
    int32_t acc = 0;
    const uint32_t *w32 = (const uint32_t *)w;
    const uint32_t *x32 = (const uint32_t *)x;
    uint32_t pairs = n / 2u;

    /* Unrolled 4x (= 8 MACs) to hide load latency; ARM926 has no out-of-order recovery. */
    uint32_t i = 0;
    for (; i + 4 <= pairs; i += 4) {
        uint32_t a0 = w32[i+0], b0 = x32[i+0];
        uint32_t a1 = w32[i+1], b1 = x32[i+1];
        uint32_t a2 = w32[i+2], b2 = x32[i+2];
        uint32_t a3 = w32[i+3], b3 = x32[i+3];
        __asm__ volatile(
            "smlabb %0, %1, %2, %0\n\t" "smlatt %0, %1, %2, %0\n\t"
            "smlabb %0, %3, %4, %0\n\t" "smlatt %0, %3, %4, %0\n\t"
            "smlabb %0, %5, %6, %0\n\t" "smlatt %0, %5, %6, %0\n\t"
            "smlabb %0, %7, %8, %0\n\t" "smlatt %0, %7, %8, %0"
            : "+r"(acc)
            : "r"(a0), "r"(b0), "r"(a1), "r"(b1), "r"(a2), "r"(b2), "r"(a3), "r"(b3));
    }
    for (uint32_t j = i * 2u; j < n; j++) acc += (int32_t)w[j] * (int32_t)x[j];
    return acc;
}

/* ---- B11b: variants, to find the real compute lever ------------------------------------------
 *
 * The first device run showed the hand-written SMLA version LOSING to naive C, 28 vs 48 MMAC/s.
 * Disassembly explains it: GCC already emits `smlabb` for the naive loop by itself, and the
 * hand-written 4x-unrolled asm added nothing but register pressure -- 4 stack spills.
 *
 * So "use the DSP MAC" was never the lever; the compiler was already using it. The naive inner loop
 * is 5 instructions per MAC (2 byte loads, compare, mac, branch) and measured ~8 cycles/MAC, so it
 * is stalling on load-use, not on the multiply.
 *
 * The real lever is therefore LOADS PER MAC. A word load brings 4 int8 values, so one LDR can feed
 * four MACs instead of one LDRSB feeding one. These variants test that directly. */

/* Word-loaded int8: one LDR per 4 values, sign-extended pairwise, 4 MACs per load pair. */
static int32_t dot_int8_word(const int8_t *w, const int8_t *x, uint32_t n) {
    int32_t acc = 0;
    const uint32_t *w32 = (const uint32_t *)w, *x32 = (const uint32_t *)x;
    uint32_t words = n / 4u, i = 0;
    for (; i < words; i++) {
        uint32_t a = w32[i], b = x32[i];
        acc += (int32_t)(int8_t)(a       ) * (int32_t)(int8_t)(b       );
        acc += (int32_t)(int8_t)(a >>  8u) * (int32_t)(int8_t)(b >>  8u);
        acc += (int32_t)(int8_t)(a >> 16u) * (int32_t)(int8_t)(b >> 16u);
        acc += (int32_t)(int8_t)(a >> 24u) * (int32_t)(int8_t)(b >> 24u);
    }
    for (uint32_t j = i * 4u; j < n; j++) acc += (int32_t)w[j] * (int32_t)x[j];
    return acc;
}

/* Same idea, but 2 words per iteration so the loads are further from their uses. ARM926 has no
 * out-of-order recovery, so distance between a load and its consumer is the whole game. */
static int32_t dot_int8_word2(const int8_t *w, const int8_t *x, uint32_t n) {
    int32_t acc = 0;
    const uint32_t *w32 = (const uint32_t *)w, *x32 = (const uint32_t *)x;
    uint32_t words = n / 4u, i = 0;
    for (; i + 2 <= words; i += 2) {
        uint32_t a0 = w32[i], b0 = x32[i], a1 = w32[i+1], b1 = x32[i+1];
        acc += (int32_t)(int8_t)(a0      ) * (int32_t)(int8_t)(b0      );
        acc += (int32_t)(int8_t)(a0 >>  8) * (int32_t)(int8_t)(b0 >>  8);
        acc += (int32_t)(int8_t)(a0 >> 16) * (int32_t)(int8_t)(b0 >> 16);
        acc += (int32_t)(int8_t)(a0 >> 24) * (int32_t)(int8_t)(b0 >> 24);
        acc += (int32_t)(int8_t)(a1      ) * (int32_t)(int8_t)(b1      );
        acc += (int32_t)(int8_t)(a1 >>  8) * (int32_t)(int8_t)(b1 >>  8);
        acc += (int32_t)(int8_t)(a1 >> 16) * (int32_t)(int8_t)(b1 >> 16);
        acc += (int32_t)(int8_t)(a1 >> 24) * (int32_t)(int8_t)(b1 >> 24);
    }
    for (uint32_t j = i * 4u; j < n; j++) acc += (int32_t)w[j] * (int32_t)x[j];
    return acc;
}

/* Plain int16 C, no asm -- checks whether the compiler beats the hand-written asm here too. */
static int32_t dot_int16_c(const int16_t *w, const int16_t *x, uint32_t n) {
    int32_t acc = 0;
    for (uint32_t i = 0; i < n; i++) acc += (int32_t)w[i] * (int32_t)x[i];
    return acc;
}

/* ---- B12: the soft-float penalty ------------------------------------------------------------ */
/* There is no FPU. Every float op is a libgcc call. This measures exactly what a stray float in the
 * hot loop costs, so "no soft-float in the hot loop" becomes a measured rule rather than folklore. */
static float dot_float(const float *w, const float *x, uint32_t n) {
    float acc = 0.0f;
    for (uint32_t i = 0; i < n; i++) acc += w[i] * x[i];
    return acc;
}

static uint32_t mmac_per_s(uint32_t ticks, uint32_t timer_hz, uint64_t macs) {
    if (!ticks) return 0;
    return (uint32_t)((macs * timer_hz) / ((uint64_t)ticks * 1000000ull));
}

int main(void) {
    bench_open("bench_mac");

    bench_timer_t t;
    timer_acquire(&t, TIMER_FAST_BASE);
    uint32_t timer_hz = apb_clock_hz();   /* derived from the PMU, not assumed */
    uint32_t cpu_hz   = cpu_clock_hz();
    bench_result("timer_hz_derived", "%lu (APB = CPU/4, from the PMU)", (unsigned long)timer_hz);
    bench_result("cpu_hz", "%lu (%s)", (unsigned long)cpu_hz, power_state_guess(cpu_hz));

    int8_t  *w8  = (int8_t  *)malloc(VEC_N);
    int8_t  *x8  = (int8_t  *)malloc(VEC_N);
    int16_t *w16 = (int16_t *)malloc(VEC_N * 2);
    int16_t *x16 = (int16_t *)malloc(VEC_N * 2);
    float   *wf  = (float   *)malloc(VEC_N * 4);
    float   *xf  = (float   *)malloc(VEC_N * 4);
    if (!w8 || !x8 || !w16 || !x16 || !wf || !xf) {
        bench_result("B10", "ALLOC FAILED"); bench_close(); return 1;
    }
    for (uint32_t i = 0; i < VEC_N; i++) {
        w8[i]  = (int8_t)(i * 7);   x8[i]  = (int8_t)(i * 3);
        w16[i] = (int16_t)(i * 7);  x16[i] = (int16_t)(i * 3);
        wf[i]  = (float)(i % 17);   xf[i]  = (float)(i % 13);
    }

    volatile int32_t sink32 = 0;
    volatile float   sinkf  = 0.0f;
    uint64_t macs = (uint64_t)VEC_N * ROWS;

    uint32_t t0 = timer_raw(t.base);
    for (uint32_t r = 0; r < ROWS; r++) sink32 += dot_int8_naive(w8, x8, VEC_N);
    uint32_t t1 = timer_raw(t.base);
    uint32_t ticks8 = timer_delta(t0, t1);
    bench_result("B10_int8_naive_MMAC_s", "%lu", (unsigned long)mmac_per_s(ticks8, timer_hz, macs));
    if (cpu_hz && ticks8)
        bench_result("B10_cycles_per_MAC", "%lu.%02lu",
                     (unsigned long)(((uint64_t)ticks8 * cpu_hz) / ((uint64_t)timer_hz * macs)),
                     (unsigned long)((((uint64_t)ticks8 * cpu_hz * 100) / ((uint64_t)timer_hz * macs)) % 100));

    t0 = timer_raw(t.base);
    for (uint32_t r = 0; r < ROWS; r++) sink32 += dot_int16_smla(w16, x16, VEC_N);
    t1 = timer_raw(t.base);
    uint32_t ticks16 = timer_delta(t0, t1);
    bench_result("B11_int16_smla_MMAC_s", "%lu", (unsigned long)mmac_per_s(ticks16, timer_hz, macs));
    if (cpu_hz && ticks16)
        bench_result("B11_cycles_per_MAC", "%lu.%02lu (architectural floor is 1.00)",
                     (unsigned long)(((uint64_t)ticks16 * cpu_hz) / ((uint64_t)timer_hz * macs)),
                     (unsigned long)((((uint64_t)ticks16 * cpu_hz * 100) / ((uint64_t)timer_hz * macs)) % 100));

    /* B11b: the variants. Same macs, same buffers, so the numbers are directly comparable. */
    t0 = timer_raw(t.base);
    for (uint32_t r = 0; r < ROWS; r++) sink32 += dot_int16_c(w16, x16, VEC_N);
    t1 = timer_raw(t.base);
    bench_result("B11b_int16_plain_C_MMAC_s", "%lu",
                 (unsigned long)mmac_per_s(timer_delta(t0, t1), timer_hz, macs));

    t0 = timer_raw(t.base);
    for (uint32_t r = 0; r < ROWS; r++) sink32 += dot_int8_word(w8, x8, VEC_N);
    t1 = timer_raw(t.base);
    bench_result("B11c_int8_word_MMAC_s", "%lu (1 LDR per 4 MACs)",
                 (unsigned long)mmac_per_s(timer_delta(t0, t1), timer_hz, macs));

    t0 = timer_raw(t.base);
    for (uint32_t r = 0; r < ROWS; r++) sink32 += dot_int8_word2(w8, x8, VEC_N);
    t1 = timer_raw(t.base);
    bench_result("B11d_int8_word2_MMAC_s", "%lu (2 words/iter, loads further from use)",
                 (unsigned long)mmac_per_s(timer_delta(t0, t1), timer_hz, macs));

    /* Float run is 64x shorter -- soft-float is slow enough that a full-length run would wrap the timer. */
    t0 = timer_raw(t.base);
    for (uint32_t r = 0; r < ROWS / 64; r++) sinkf += dot_float(wf, xf, VEC_N);
    t1 = timer_raw(t.base);
    uint32_t ticksf = timer_delta(t0, t1);
    bench_result("B12_softfloat_MMAC_s", "%lu",
                 (unsigned long)mmac_per_s(ticksf, timer_hz, macs / 64));
    if (ticks16 && ticksf)
        bench_result("B12_softfloat_slowdown_vs_B11", "%lux",
                     (unsigned long)(((uint64_t)ticksf * 64) / ticks16));

    __asm__ volatile("" :: "r"(sink32), "r"(sinkf) : "memory");

    /* The line that decides the project's architecture. */
    bench_result("D4_crossover_note",
                 "compare B11 against B4 from bench_mem: int4 is compute-bound iff B11 < 2*B4/bytes_per_mac");

    free(w8); free(x8); free(w16); free(x16); free(wf); free(xf);
    timer_release(&t);
    bench_close();
    return 0;
}
