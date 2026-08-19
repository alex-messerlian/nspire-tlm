/* bench_platform.c — fills HARDWARE.md rows A1-A12, C1-C8.
 *
 * "What machine am I actually on?" Run this first; everything else depends on it.
 *
 * NOT YET COMPILED. Written before the toolchain exists; expect breakage on first build.
 */
#include "common.h"

/* ---- CP15 identification ------------------------------------------------------------------- */
/* Ndless programs run privileged, so MRC p15 should be legal. If the device faults here, delete
 * probe_cache_cp15() and rely on the stride-timing probe below, which needs no privilege. */

static inline uint32_t cp15_main_id(void) {
    uint32_t v; __asm__ volatile("mrc p15, 0, %0, c0, c0, 0" : "=r"(v)); return v;
}
static inline uint32_t cp15_cache_type(void) {
    uint32_t v; __asm__ volatile("mrc p15, 0, %0, c0, c0, 1" : "=r"(v)); return v;
}
static inline uint32_t cp15_control(void) {
    uint32_t v; __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(v)); return v;
}

/* ARMv5 Cache Type Register decode: size = 512 << field, line = 8 << field. */
static void decode_cache_half(const char *which, uint32_t size_f, uint32_t assoc_f,
                              uint32_t m, uint32_t len_f) {
    uint32_t bytes = 512u << size_f;
    uint32_t line  = 8u   << len_f;
    uint32_t ways  = (assoc_f == 0 && m == 0) ? 1u : (1u << assoc_f);
    bench_result(which, "%lu bytes, %lu-byte line, %lu-way (raw size_f=%lu assoc_f=%lu M=%lu len_f=%lu)",
                 (unsigned long)bytes, (unsigned long)line, (unsigned long)ways,
                 (unsigned long)size_f, (unsigned long)assoc_f,
                 (unsigned long)m, (unsigned long)len_f);
}

static void probe_cache_cp15(void) {
    uint32_t ct = cp15_cache_type();
    bench_result("cp15_main_id",    "0x%08lX", (unsigned long)cp15_main_id());
    bench_result("cp15_cache_type", "0x%08lX", (unsigned long)ct);
    bench_result("cp15_control",    "0x%08lX", (unsigned long)cp15_control());
    bench_result("caches_separate", "%s", ((ct >> 24) & 1u) ? "yes (Harvard)" : "no (unified)");
    decode_cache_half("A11_dcache", (ct >> 18) & 0xFu, (ct >> 15) & 0x7u,
                      (ct >> 14) & 1u, (ct >> 12) & 0x3u);
    decode_cache_half("A10_icache", (ct >>  6) & 0xFu, (ct >>  3) & 0x7u,
                      (ct >>  2) & 1u, (ct >>  0) & 0x3u);
}

/* ---- Independent cache-size probe by stride timing ------------------------------------------ */
/* Cross-check on the CP15 decode. Pointer-chase over working sets of increasing size; per-access
 * latency steps up when the set stops fitting. Pointer chasing (not indexing) forces one dependent
 * load at a time, which defeats any prefetching or load pipelining. */

#define CHASE_STEPS    9u          /* 1, 2, 4, ... 256 KB */
#define CHASE_ACCESSES 200000u

static void probe_cache_stride(bench_timer_t *t, uint32_t timer_hz) {
    const uint32_t stride = 64;    /* >= any plausible line size, so every access is a fresh line */

    for (uint32_t step = 0, kb = 1; step < CHASE_STEPS; step++, kb *= 2) {
        uint32_t bytes = kb * 1024u;
        void **buf = (void **)malloc(bytes);
        if (!buf) { bench_result("A12_chase", "%lu KB: ALLOC FAILED", (unsigned long)kb); continue; }

        uint32_t n = bytes / stride;
        for (uint32_t i = 0; i < n; i++)
            buf[(i * stride) / sizeof(void *)] =
                &buf[(((i + 1) % n) * stride) / sizeof(void *)];

        void **p = buf;
        for (uint32_t i = 0; i < n * 4; i++) p = (void **)*p;       /* warm */

        uint32_t t0 = timer_raw(t->base);
        for (uint32_t i = 0; i < CHASE_ACCESSES; i++) p = (void **)*p;
        uint32_t t1 = timer_raw(t->base);
        __asm__ volatile("" :: "r"(p) : "memory");                  /* keep p live, defeat DCE */

        uint32_t ticks = timer_delta(t0, t1);
        uint32_t ns_x100 = (uint32_t)(((uint64_t)ticks * 100000000000ull) /
                                      ((uint64_t)timer_hz * CHASE_ACCESSES));
        bench_result("A12_chase", "%lu KB: %lu.%02lu ns/access (ticks=%lu)",
                     (unsigned long)kb, (unsigned long)(ns_x100 / 100),
                     (unsigned long)(ns_x100 % 100), (unsigned long)ticks);
        free(buf);
    }
    bench_result("A12_note",
                 "cache size = largest KB before the latency step; line size needs a separate stride sweep");
}

/* ---- Timer validation ----------------------------------------------------------------------- */
/* C8: we do not trust "99 MHz" from a wiki. Gate the fast timer against the 32.768 kHz timer,
 * which is crystal-derived and therefore the more trustworthy of the two. */

static uint32_t validate_timers(bench_timer_t *fast, bench_timer_t *slow) {
    const uint32_t SLOW_HZ    = 32768u;
    const uint32_t GATE_TICKS = SLOW_HZ * 2u;  /* ~2 s: long enough to average, short enough not to
                                                * wrap a 32-bit counter at ~99 MHz (wraps at ~43 s) */
    uint32_t s0 = timer_raw(slow->base);
    uint32_t f0 = timer_raw(fast->base);
    while (timer_delta(s0, timer_raw(slow->base)) < GATE_TICKS) { /* spin */ }
    uint32_t f1 = timer_raw(fast->base);
    uint32_t s1 = timer_raw(slow->base);

    uint32_t slow_ticks = timer_delta(s0, s1);
    uint32_t fast_ticks = timer_delta(f0, f1);
    if (slow_ticks == 0) { bench_result("C1_fast_timer_measured_hz", "FAILED: 32k timer did not tick"); return 0; }
    uint32_t measured_hz = (uint32_t)(((uint64_t)fast_ticks * SLOW_HZ) / slow_ticks);

    bench_result("C1_fast_timer_measured_hz", "%lu (wiki claims 99000000)", (unsigned long)measured_hz);
    if (measured_hz) {
        bench_result("C5_resolution_ns", "%lu.%02lu",
                     (unsigned long)(1000000000ul / measured_hz),
                     (unsigned long)((100000000000ull / measured_hz) % 100));
        bench_result("C6_wrap_period_s", "%lu -- runs longer than this MUST handle wrap",
                     (unsigned long)(0xFFFFFFFFul / measured_hz));
    }
    bench_result("C7_timer_owned_by_os", "fast=%s slow=%s",
                 fast->we_configured_it ? "no (we configured it)" : "YES (was already running)",
                 slow->we_configured_it ? "no (we configured it)" : "YES (was already running)");
    return measured_hz;
}

int main(void) {
    bench_open("bench_platform");

    bench_result("A1_model_id_900A0000", "0x%08lX (expect 0x202 for CX II)",
                 (unsigned long)MMIO32(0x900A0000u));
    bench_result("hwtype", "%lu", (unsigned long)hwtype());
    bench_result("is_cx2",  "%d", (int)is_cx2);

    uint32_t hz = cpu_clock_hz();
    bench_result("A3_cpu_clock_hz", "%lu", (unsigned long)hz);
    bench_result("power_state",     "%s", power_state_guess(hz));
    bench_result("pmu_raw_0x30",  "0x%08lX", (unsigned long)MMIO32(PMU_CLK_MAIN));
    bench_result("pmu_raw_0x20",  "0x%08lX", (unsigned long)MMIO32(PMU_CLK_DIV2));
    bench_result("pmu_raw_0x810", "0x%08lX", (unsigned long)MMIO32(PMU_CLK_GATE));

    probe_cache_cp15();

    bench_timer_t fast, slow;
    timer_acquire(&fast, TIMER_FAST_BASE);
    timer_acquire(&slow, TIMER_32K_BASE);
    uint32_t timer_hz = validate_timers(&fast, &slow);
    if (timer_hz) probe_cache_stride(&fast, timer_hz);

    timer_release(&slow);
    timer_release(&fast);
    bench_close();
    return 0;
}
