/* bench_mem.c: fills HARDWARE.md rows B1-B7 and A7-A8.
 *
 * These are the numbers the entire project rests on. B1 (largest malloc) sets the hard ceiling on
 * model size; B4 (streaming read bandwidth) sets the memory-bound throughput ceiling.
 *
 * NOT YET COMPILED.
 */
#include "common.h"

/* ---- B1/B2/B3: how much memory can an Ndless application actually get? ---------------------- */

/* B1: binary search for the largest single malloc that succeeds.
 * malloc succeeding is not the same as the memory being usable -- the OS may overcommit -- so we
 * touch one byte per 4 KB page before declaring victory. */
static uint32_t probe_largest_malloc(void) {
    uint32_t lo = 0, hi = 64u * 1024u * 1024u;   /* upper bound: total physical RAM */
    while (hi - lo > 64u * 1024u) {              /* 64 KB resolution */
        uint32_t mid = lo + (hi - lo) / 2u;
        void *p = malloc(mid);
        if (p) {
            volatile unsigned char *b = (volatile unsigned char *)p;
            for (uint32_t i = 0; i < mid; i += 4096u) b[i] = (unsigned char)i;
            for (uint32_t i = 0; i < mid; i += 4096u) if (b[i] != (unsigned char)i) { free(p); hi = mid; goto next; }
            free(p);
            lo = mid;
        } else {
            hi = mid;
        }
    next: ;
    }
    return lo;
}

/* B2: total allocatable in chunks. If this greatly exceeds B1, the heap is fragmented and weights
 * must be allocated per-layer rather than as one contiguous block. That is a design decision this
 * measurement makes for us. */
#define MAX_CHUNKS 512
static void probe_total_heap(uint32_t chunk) {
    void *chunks[MAX_CHUNKS];
    int n = 0;
    uint64_t total = 0;
    while (n < MAX_CHUNKS) {
        void *p = malloc(chunk);
        if (!p) break;
        chunks[n++] = p;
        total += chunk;
    }
    for (int i = 0; i < n; i++) free(chunks[i]);
    bench_result("B2_total_heap_bytes", "%lu in %d x %lu-byte chunks",
                 (unsigned long)total, n, (unsigned long)chunk);
}

/* ---- B4/B5: bandwidth ----------------------------------------------------------------------- */
/* Streaming read. LDMIA pulls 8 words (32 B) per instruction, which is the fastest a 926 can pull
 * from memory and matches the likely cache line size. We sum into an accumulator so the compiler
 * cannot delete the loads.
 *
 * The buffer must be several times larger than D-cache or this measures cache, not DRAM. */
#define BW_BUF_BYTES (4u * 1024u * 1024u)
#define BW_REPS      4u

static uint32_t stream_read_sum(const uint32_t *p, uint32_t words) {
    uint32_t acc = 0;
    /* 8 words per iteration; gcc emits ldmia for this on ARM at -O2/-O3. Verified by reading the
     * disassembly, NOT assumed -- see bench/README.md. */
    for (uint32_t i = 0; i < words; i += 8) {
        acc += p[i+0] + p[i+1] + p[i+2] + p[i+3] +
               p[i+4] + p[i+5] + p[i+6] + p[i+7];
    }
    return acc;
}

static uint32_t bandwidth_mb_s(uint32_t ticks, uint32_t timer_hz, uint64_t bytes) {
    if (!ticks) return 0;
    /* MB/s = bytes / (ticks / timer_hz) / 1e6 */
    return (uint32_t)((bytes * timer_hz) / ((uint64_t)ticks * 1000000ull));
}

static void probe_bandwidth(bench_timer_t *t, uint32_t timer_hz) {
    uint32_t *buf = (uint32_t *)malloc(BW_BUF_BYTES);
    if (!buf) { bench_result("B4_dram_read_MB_s", "ALLOC FAILED at %lu bytes", (unsigned long)BW_BUF_BYTES); return; }
    for (uint32_t i = 0; i < BW_BUF_BYTES / 4; i++) buf[i] = i;

    /* B4: streaming read */
    uint32_t t0 = timer_raw(t->base);
    uint32_t acc = 0;
    for (uint32_t r = 0; r < BW_REPS; r++) acc += stream_read_sum(buf, BW_BUF_BYTES / 4);
    uint32_t t1 = timer_raw(t->base);
    __asm__ volatile("" :: "r"(acc) : "memory");
    bench_result("B4_dram_read_MB_s", "%lu (buf=%lu KB, reps=%lu, ticks=%lu)",
                 (unsigned long)bandwidth_mb_s(timer_delta(t0, t1), timer_hz,
                                               (uint64_t)BW_BUF_BYTES * BW_REPS),
                 (unsigned long)(BW_BUF_BYTES / 1024), (unsigned long)BW_REPS,
                 (unsigned long)timer_delta(t0, t1));

    /* B5: memcpy, half the buffer to the other half. Counts bytes MOVED (read+write = 2x). */
    uint32_t half = BW_BUF_BYTES / 2;
    t0 = timer_raw(t->base);
    for (uint32_t r = 0; r < BW_REPS; r++)
        memcpy((unsigned char *)buf + half, buf, half);
    t1 = timer_raw(t->base);
    bench_result("B5_memcpy_MB_s", "%lu (counting bytes copied, not touched)",
                 (unsigned long)bandwidth_mb_s(timer_delta(t0, t1), timer_hz,
                                               (uint64_t)half * BW_REPS));
    free(buf);
}

/* ---- B6/B7: is the 256 KB internal SRAM at 0xA4000000 reachable? ---------------------------- */
/* Nothing in the brief considered this. If an Ndless app can claim even 64 KB of it, that is the
 * right home for activations, RMSNorm scratch, and the hot rows of the KV cache -- all of which are
 * touched many times per token. Unknown whether the OS owns all of it.
 *
 * DANGER: this writes to memory that may belong to the OS. Run it LAST, and expect a reset.
 * Read-probe first; only write to a region that reads back as an obvious unused pattern. */
static void probe_sram(bench_timer_t *t, uint32_t timer_hz) {
    volatile uint32_t *sram = (volatile uint32_t *)(uintptr_t)ADDR_SRAM_BASE;

    /* Read probe only -- report what is there so a human can decide what is safe to clobber. */
    bench_result("B7_sram_read_probe", "0x%08lX 0x%08lX 0x%08lX 0x%08lX (offsets 0, 64K, 128K, 192K)",
                 (unsigned long)sram[0],
                 (unsigned long)sram[(64u  * 1024u) / 4],
                 (unsigned long)sram[(128u * 1024u) / 4],
                 (unsigned long)sram[(192u * 1024u) / 4]);

    /* B6: read bandwidth from SRAM. Read-only, so this is safe even if the OS owns the region. */
    const uint32_t words = (32u * 1024u) / 4u;   /* 32 KB window, read-only */
    uint32_t t0 = timer_raw(t->base);
    uint32_t acc = 0;
    for (uint32_t r = 0; r < 256; r++)
        for (uint32_t i = 0; i < words; i += 8)
            acc += sram[i+0] + sram[i+1] + sram[i+2] + sram[i+3] +
                   sram[i+4] + sram[i+5] + sram[i+6] + sram[i+7];
    uint32_t t1 = timer_raw(t->base);
    __asm__ volatile("" :: "r"(acc) : "memory");
    bench_result("B6_sram_read_MB_s", "%lu (32 KB window x 256 -- likely cache-resident, see note)",
                 (unsigned long)bandwidth_mb_s(timer_delta(t0, t1), timer_hz, 32ull * 1024 * 256));
    bench_result("B6_note",
                 "if B6 ~= B4 the window fit in D-cache and this measured cache, not SRAM; enlarge the window past A11 and rerun");
    bench_result("B7_sram_writable", "NOT TESTED -- writing here may crash the OS; decide by hand from the read probe");
}

int main(void) {
    bench_open("bench_mem");

    bench_timer_t t;
    timer_acquire(&t, TIMER_FAST_BASE);
    uint32_t timer_hz = apb_clock_hz();   /* derived from the PMU, not assumed */
    bench_result("timer_hz_derived", "%lu (APB = CPU/4, from the PMU)", (unsigned long)timer_hz);

    uint32_t largest = probe_largest_malloc();
    bench_result("B1_largest_malloc_bytes", "%lu (%lu MB)",
                 (unsigned long)largest, (unsigned long)(largest / (1024u * 1024u)));
    probe_total_heap(1u * 1024u * 1024u);
    probe_total_heap(64u * 1024u);

    probe_bandwidth(&t, timer_hz);
    probe_sram(&t, timer_hz);

    timer_release(&t);
    bench_close();
    return 0;
}
