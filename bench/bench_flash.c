/* bench_flash.c — fills HARDWARE.md rows B8-B9 and A9.
 *
 * Why this matters: the brief states that any weight living in flash rather than RAM is a
 * catastrophe. That is true for a DENSE model, where every weight is read every token. It is NOT
 * true for a PLE-style sparse embedding table, where ~450 bytes per token come off flash and the
 * design works fine (see docs/PRIOR_ART.md section 2, the 28.9M-param ESP32-S3 result).
 *
 * So we measure two different things:
 *   B8 sequential bandwidth  -> tells us the cost of streaming weights (the catastrophe case)
 *   B9 random 4 KB latency   -> tells us whether the PLE escape hatch is open on this device
 *
 * NOT YET COMPILED.
 */
#include "common.h"

#define FLASH_TEST_FILE "/documents/bench/blob.bin.tns"
#define BLOB_MB         8u
#define SEQ_CHUNK       (64u * 1024u)
#define RAND_READS      256u
#define RAND_SIZE       4096u

/* The OS caches file data. A second read of the same bytes measures cache, not flash. Every result
 * below is therefore a COLD-ish first read, and we say so rather than pretending otherwise. */

static int make_blob(void) {
    FILE *f = fopen(FLASH_TEST_FILE, "rb");
    if (f) { fclose(f); return 1; }              /* already there */
    f = fopen(FLASH_TEST_FILE, "wb");
    if (!f) return 0;
    unsigned char *chunk = (unsigned char *)malloc(SEQ_CHUNK);
    if (!chunk) { fclose(f); return 0; }
    for (uint32_t i = 0; i < SEQ_CHUNK; i++) chunk[i] = (unsigned char)(i * 31u);
    for (uint32_t i = 0; i < (BLOB_MB * 1024u * 1024u) / SEQ_CHUNK; i++) {
        if (fwrite(chunk, 1, SEQ_CHUNK, f) != SEQ_CHUNK) { free(chunk); fclose(f); return 0; }
    }
    free(chunk);
    fclose(f);
    return 1;
}

int main(void) {
    bench_open("bench_flash");

    bench_timer_t t;
    timer_acquire(&t, TIMER_FAST_BASE);
    uint32_t timer_hz = 99000000u;   /* TODO: use bench_platform's measured C1 */
    bench_result("timer_hz_assumed", "%lu -- REPLACE with measured C1", (unsigned long)timer_hz);

    if (!make_blob()) { bench_result("B8_flash_seq_MB_s", "BLOB CREATION FAILED"); bench_close(); return 1; }

    unsigned char *buf = (unsigned char *)malloc(SEQ_CHUNK);
    if (!buf) { bench_result("B8_flash_seq_MB_s", "ALLOC FAILED"); bench_close(); return 1; }

    /* B8: sequential read of the whole blob. */
    FILE *f = fopen(FLASH_TEST_FILE, "rb");
    if (!f) { bench_result("B8_flash_seq_MB_s", "OPEN FAILED"); bench_close(); return 1; }
    uint64_t total = 0;
    uint32_t t0 = timer_raw(t.base);
    size_t n;
    while ((n = fread(buf, 1, SEQ_CHUNK, f)) > 0) total += n;
    uint32_t t1 = timer_raw(t.base);
    fclose(f);
    uint32_t ticks = timer_delta(t0, t1);
    bench_result("B8_flash_seq_MB_s", "%lu (%lu bytes, %lu KB chunks, first read after write -- may be OS-cached)",
                 (unsigned long)(ticks ? (total * timer_hz) / ((uint64_t)ticks * 1000000ull) : 0),
                 (unsigned long)total, (unsigned long)(SEQ_CHUNK / 1024));

    /* B9: random 4 KB reads, the PLE access pattern. */
    f = fopen(FLASH_TEST_FILE, "rb");
    if (f) {
        uint32_t span = BLOB_MB * 1024u * 1024u - RAND_SIZE;
        uint32_t seed = 12345u;
        t0 = timer_raw(t.base);
        for (uint32_t i = 0; i < RAND_READS; i++) {
            seed = seed * 1103515245u + 12345u;          /* deterministic, so runs are comparable */
            fseek(f, (long)(seed % span), SEEK_SET);
            if (fread(buf, 1, RAND_SIZE, f) != RAND_SIZE) break;
        }
        t1 = timer_raw(t.base);
        fclose(f);
        ticks = timer_delta(t0, t1);
        uint32_t us_x10 = (uint32_t)(((uint64_t)ticks * 10000000ull) / ((uint64_t)timer_hz * RAND_READS));
        bench_result("B9_flash_rand4k_us", "%lu.%lu per %lu-byte read (%lu reads)",
                     (unsigned long)(us_x10 / 10), (unsigned long)(us_x10 % 10),
                     (unsigned long)RAND_SIZE, (unsigned long)RAND_READS);
        bench_result("B9_ple_verdict",
                     "PLE viable iff this x rows_per_token stays well under the per-token budget; at 2 tok/s that budget is 500 ms");
    }

    free(buf);
    timer_release(&t);
    bench_close();
    return 0;
}
