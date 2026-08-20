/* common.h — shared measurement infrastructure for Phase 0 micro-benchmarks.
 *
 * NOT YET COMPILED. Written before the toolchain exists; expect breakage on first build.
 *
 * Two non-obvious constraints drive this file's design:
 *
 *  1. Every benchmark must run with USB DISCONNECTED, because attaching USB drops the CPU from
 *     396 MHz to 288 MHz. That means we cannot watch stdout. All results are appended to a log
 *     file on flash, which the operator reads back afterwards. Screen output is a convenience only.
 *
 *  2. We must not TRUST that USB is disconnected. Every record stamps the CPU clock read live from
 *     the PMU, so a run accidentally taken at 288 MHz is self-identifying in the log rather than
 *     silently poisoning the dataset.
 */
#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <libndls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

/* ---- Memory map (Hackspire, CX II). See docs/HARDWARE.md section A. ------------------------- */
#define ADDR_SDRAM_BASE     0x10000000u  /* 64 MB LPDDR                                          */
#define ADDR_SRAM_BASE      0xA4000000u  /* 256 KB internal SRAM — usability UNMEASURED (B7)      */
#define ADDR_SRAM_SIZE      (256u * 1024u)

/* ---- SP804 timers -------------------------------------------------------------------------- */
#define TIMER_FAST_BASE     0x90010000u  /* APB clock, ~99 MHz default                            */
#define TIMER_12M_BASE      0x900C0000u  /* 12 MHz default                                        */
#define TIMER_32K_BASE      0x900D0000u  /* 32.768 kHz default — our independent cross-check      */

#define SP804_LOAD          0x00u
#define SP804_VALUE         0x04u        /* counts DOWN                                           */
#define SP804_CONTROL       0x08u
#define SP804_INTCLR        0x0Cu
#define SP804_BGLOAD        0x18u

#define SP804_CTRL_ONESHOT  (1u << 0)
#define SP804_CTRL_32BIT    (1u << 1)
#define SP804_CTRL_PRE_1    (0u << 2)
#define SP804_CTRL_IE       (1u << 5)
#define SP804_CTRL_PERIODIC (1u << 6)
#define SP804_CTRL_ENABLE   (1u << 7)

/* Free-running, 32-bit, no prescale, no interrupt. */
#define SP804_CTRL_FREERUN  (SP804_CTRL_ENABLE | SP804_CTRL_32BIT | SP804_CTRL_PRE_1)

/* ---- PMU clock registers (Zephray). See docs/HARDWARE.md A3–A6. ---------------------------- */
#define PMU_BASE            0x90140000u
#define PMU_CLK_MAIN        (PMU_BASE + 0x30u)  /* [31:24] mult, [21:16] div1, bit4 gates div2   */
#define PMU_CLK_DIV2        (PMU_BASE + 0x20u)  /* [23:20] div2                                  */
#define PMU_CLK_GATE        (PMU_BASE + 0x810u) /* bit4 enables div2                             */
#define PMU_XTAL_HZ         12000000u

#define MMIO32(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* ---- Timer -------------------------------------------------------------------------------- */

/* State captured so we can leave the timer exactly as we found it. The OS may own it (C7). */
typedef struct {
    uint32_t base;
    uint32_t saved_control;
    uint32_t saved_load;
    int      we_configured_it;
} bench_timer_t;

static inline uint32_t timer_raw(uint32_t base) {
    return MMIO32(base + SP804_VALUE);
}

/* Acquire a timer for free-running 32-bit use.
 * If it is ALREADY enabled in free-running 32-bit mode we leave it alone and just read it — that is
 * almost certainly the OS's own timebase and stopping it would be rude and probably fatal. */
static inline void timer_acquire(bench_timer_t *t, uint32_t base) {
    t->base            = base;
    t->saved_control   = MMIO32(base + SP804_CONTROL);
    t->saved_load      = MMIO32(base + SP804_LOAD);
    t->we_configured_it = 0;

    const uint32_t want = SP804_CTRL_ENABLE | SP804_CTRL_32BIT;
    int already_ok = ((t->saved_control & want) == want) &&
                     !(t->saved_control & SP804_CTRL_PERIODIC) &&
                     !(t->saved_control & SP804_CTRL_ONESHOT);
    if (already_ok) return;

    MMIO32(base + SP804_CONTROL) = 0;                 /* disable while reconfiguring */
    MMIO32(base + SP804_LOAD)    = 0xFFFFFFFFu;
    MMIO32(base + SP804_CONTROL) = SP804_CTRL_FREERUN;
    t->we_configured_it = 1;
}

static inline void timer_release(bench_timer_t *t) {
    if (!t->we_configured_it) return;
    MMIO32(t->base + SP804_CONTROL) = 0;
    MMIO32(t->base + SP804_LOAD)    = t->saved_load;
    MMIO32(t->base + SP804_CONTROL) = t->saved_control;
}

/* Ticks elapsed between two raw reads of a DOWN-counter, correct across one wrap. */
static inline uint32_t timer_delta(uint32_t start_raw, uint32_t end_raw) {
    return start_raw - end_raw;   /* unsigned wraparound does the right thing */
}

/* ---- CPU clock, read live from the PMU ----------------------------------------------------- */
/* Zephray: (12 MHz * mult / div1) / (1 + div2), div2 applying only when bit4 of PMU_CLK_GATE is set
 * AND bit4 of PMU_CLK_MAIN is clear.
 *
 * CORRECTED 2026-08-19: this formula yields the **AHB** clock, not the CPU clock. The documented
 * tree is CPU 396 / AHB 198 / APB 99 -- AHB = CPU/2, APB = CPU/4. Confirmed on hardware to 0.02%:
 * PMU-derived 198 with the crystal-gated timer reading 98.98 MHz, and PMU-derived 144 with the timer
 * reading 72.00 MHz.
 *
 * Calling the raw value "cpu_hz" made a perfectly normal 396/288 device look like it was throttling
 * to 198/144, and produced a session's worth of wrong conclusions. Hence three explicit accessors. */
static inline uint32_t ahb_clock_hz(void) {
    uint32_t main = MMIO32(PMU_CLK_MAIN);
    uint32_t mult = (main >> 24) & 0xFFu;
    uint32_t div1 = (main >> 16) & 0x3Fu;
    if (div1 == 0 || mult == 0) return 0;

    uint32_t hz = (uint32_t)(((uint64_t)PMU_XTAL_HZ * mult) / div1);

    int div2_active = ((MMIO32(PMU_CLK_GATE) >> 4) & 1u) && !((main >> 4) & 1u);
    if (div2_active) {
        uint32_t div2 = (MMIO32(PMU_CLK_DIV2) >> 20) & 0xFu;
        hz /= (div2 + 1u);
    }
    return hz;
}

/* Heuristic only — the log records the raw Hz too, so this never hides anything. */
/* The number that matters for compute. */
static inline uint32_t cpu_clock_hz(void) { return ahb_clock_hz() * 2u; }

/* What the SP804 timers actually count at. DERIVE it -- do not assume 99 MHz. Assuming it is why
 * every rate in the first device session needed a 1.375x correction. */
static inline uint32_t apb_clock_hz(void) { return ahb_clock_hz() / 2u; }

static inline const char *power_state_guess(uint32_t hz) {
    if (hz == 0)                       return "UNKNOWN";
    if (hz > 380000000u && hz < 410000000u) return "BATTERY(396) -- valid";
    if (hz > 270000000u && hz < 310000000u) return "TETHERED(288) -- USB or CHARGER, timings suspect";
    return "UNEXPECTED -- investigate before trusting any timing";
}

/* ---- Result logging ------------------------------------------------------------------------ */
/* Ndless writes to the calculator filesystem; the .tns extension is mandatory for the file to be
 * visible to the OS file browser, which is how the operator retrieves it. */
#define BENCH_LOG_PATH "/documents/bench/results.txt.tns"

static FILE *g_log = NULL;

static inline void bench_open(const char *bench_name) {
    g_log = fopen(BENCH_LOG_PATH, "a");
    uint32_t hz = cpu_clock_hz();
    printf("=== %s ===\n", bench_name);
    printf("cpu %lu Hz (%s)\n", (unsigned long)hz, power_state_guess(hz));
    if (g_log) {
        fprintf(g_log, "\n=== %s ===\n", bench_name);
        fprintf(g_log, "cpu_hz=%lu ahb_hz=%lu apb_hz=%lu power=%s\n",
                (unsigned long)hz, (unsigned long)ahb_clock_hz(),
                (unsigned long)apb_clock_hz(), power_state_guess(hz));
        fflush(g_log);
    }
}

/* Every result line carries its own units and conditions. A bare number is not a result. */
static inline void bench_result(const char *key, const char *fmt, ...) {
    va_list ap;
    char buf[192];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("%s = %s\n", key, buf);
    if (g_log) {
        fprintf(g_log, "%s=%s\n", key, buf);
        fflush(g_log);   /* crash-safe: probe_sram() may reset the device, and an unflushed
                          * result is a measurement that never happened */
    }
}

static inline void bench_close(void) {
    if (g_log) { fflush(g_log); fclose(g_log); g_log = NULL; }
    printf("\nlog: %s\nPress any key.\n", BENCH_LOG_PATH);
    wait_key_pressed();
}

#endif /* BENCH_COMMON_H */
