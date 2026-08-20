/* nspire.c -- implementation of the porting seam. See nspire.h for why each piece exists. */
#include "nspire.h"
#include <string.h>
#undef mmap
#undef munmap
#undef clock_gettime
#include <stdlib.h>
#include <stdio.h>
#include "nspire_screen.h"
#include <unistd.h>

extern FILE *g_nspire_log;   /* progress goes to the log too, so a reset still leaves evidence */

/* ---- mmap: malloc + read ------------------------------------------------------------------- */
/* runq.c mmaps the checkpoint once and munmaps it once, so a single-slot record is enough. Keeping
 * the pointer lets munmap() free the right thing without the caller changing. */
static void  *g_map_ptr  = NULL;
static size_t g_map_len  = 0;

/* The checkpoint path, stashed by the ported read_checkpoint. */
static char g_ckpt_path[256];
void nspire_set_checkpoint_path(const char *p) {
    if (!p) { g_ckpt_path[0] = 0; return; }
    strncpy(g_ckpt_path, p, sizeof g_ckpt_path - 1);
    g_ckpt_path[sizeof g_ckpt_path - 1] = 0;
}

void *nspire_mmap(void *addr, size_t len, int prot, int flags, int fildes, long off) {
    (void)addr; (void)prot; (void)flags;
    void *p = malloc(len);
    if (!p) {
        /* Print via printf, NOT stderr: stderr is not routed to the nspireio console, so an
         * out-of-memory message on stderr is invisible and looks identical to a hang. */
        printf("OUT OF MEMORY: needed %lu bytes\n", (unsigned long)len);
        if (g_nspire_log) { fprintf(g_nspire_log, "error=oom bytes=%lu\n", (unsigned long)len); fflush(g_nspire_log); }
        screen_flush();
        return MAP_FAILED;
    }
    (void)fildes;   /* deliberately unused -- see below */

    /* MEASURED 2026-08-19: random 4 KB flash access on this device is 58 ms, and sequential is only
     * 2 MB/s. The first device run of this program showed a blank screen for 17 minutes before a
     * hard reset. 17.1 MB in 4 KB requests is ~4 min; in 512 B requests it is ~32 min. The observed
     * 17 min sits between them, so the leading hypothesis is that raw read() was issuing small
     * requests, not that anything hung.
     *
     * So: go through a FILE* with a large explicit buffer, which coalesces into big sequential
     * reads, and report progress so a slow load can never again be mistaken for a hang. */
    /* 64 KB, matching bench_flash, which is the only read path proven to work on this device. */
    #define CHUNK (64u * 1024u)
    /* MEASURED: fdopen() on the descriptor HANGS on this device -- the first read never returned
     * after 30 minutes, and no progress line was ever printed. bench_flash reads an 8 MB file
     * happily using plain fopen() with 64 KB chunks, so use exactly that proven route. The fd
     * runq.c opened is left alone; runq.c closes it itself. */
    if (!g_ckpt_path[0]) { free(p); return MAP_FAILED; }
    FILE *fp = fopen(g_ckpt_path, "rb");
    if (!fp) {
        printf("cannot open %s\n", g_ckpt_path);
        screen_flush();
        free(p);
        return MAP_FAILED;
    }
    if (off) fseek(fp, off, SEEK_SET);

    unsigned char *q = (unsigned char *)p;
    size_t remaining = len, done = 0;
    unsigned last_pct = 999;
    while (remaining) {
        size_t want = remaining > CHUNK ? CHUNK : remaining;
        size_t got = fread(q, 1, want, fp);
        if (got == 0) { free(p); return MAP_FAILED; }
        q += got; remaining -= got; done += got;
        unsigned pct = (unsigned)((done * 100) / len);
        if (pct / 5 != last_pct / 5) {            /* every 5%, so a stall is localised quickly */
            last_pct = pct;
            printf("loading %u%%\n", pct);
            screen_flush();
            if (g_nspire_log) { fprintf(g_nspire_log, "load_pct=%u\n", pct); fflush(g_nspire_log); }
        }
    }
    fclose(fp);
    g_map_ptr = p;
    g_map_len = len;
    return p;
}

int nspire_munmap(void *addr, size_t len) {
    (void)len;
    if (addr && addr != MAP_FAILED) free(addr);
    if (addr == g_map_ptr) { g_map_ptr = NULL; g_map_len = 0; }
    return 0;
}

/* ---- clock_gettime: SP804 fast timer ------------------------------------------------------- */
#define TIMER_FAST_BASE 0x90010000u
#define SP804_VALUE     0x04u
#define SP804_CONTROL   0x08u
#define SP804_LOAD      0x00u
#define SP804_FREERUN   ((1u << 7) | (1u << 1))     /* enable | 32-bit | prescale 1 */
#define PMU_CLK_MAIN    0x90140030u
#define PMU_CLK_DIV2    0x90140020u
#define PMU_CLK_GATE    0x90140810u
#define MMIO32(a)       (*(volatile uint32_t *)(uintptr_t)(a))

/* Assumed until bench_platform measures it (HARDWARE.md C1). Every run prints it so a wrong
 * assumption is visible in the log rather than silently scaling every tok/s number. */
#define ASSUMED_TIMER_HZ 99000000u

#ifdef NSPIRE_HOST_TEST
/* Host build of the SAME port, so the malloc+read replacement for mmap can be validated against the
 * golden output before the device is involved. Only the MMIO-backed pieces are swapped -- the file
 * loading path under test is byte-identical to what the device runs. */
#include <sys/time.h>
int nspire_clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;
    struct timeval tv; gettimeofday(&tv, NULL);
    tp->tv_sec = tv.tv_sec; tp->tv_nsec = tv.tv_usec * 1000L;
    return 0;
}
unsigned nspire_timer_hz(void) { return 1000000u; }
unsigned nspire_cpu_hz(void)   { return 0u; }
#else
static int      g_timer_ready = 0;
static uint32_t g_last_raw    = 0;
static uint64_t g_ticks       = 0;

static void timer_init(void) {
    uint32_t ctrl = MMIO32(TIMER_FAST_BASE + SP804_CONTROL);
    const uint32_t want = (1u << 7) | (1u << 1);
    if ((ctrl & want) != want) {                 /* not already free-running: claim it */
        MMIO32(TIMER_FAST_BASE + SP804_CONTROL) = 0;
        MMIO32(TIMER_FAST_BASE + SP804_LOAD)    = 0xFFFFFFFFu;
        MMIO32(TIMER_FAST_BASE + SP804_CONTROL) = SP804_FREERUN;
    }
    g_last_raw    = MMIO32(TIMER_FAST_BASE + SP804_VALUE);
    g_ticks       = 0;
    g_timer_ready = 1;
}

int nspire_clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;
    if (!g_timer_ready) timer_init();
    uint32_t raw = MMIO32(TIMER_FAST_BASE + SP804_VALUE);
    g_ticks    += (uint64_t)(uint32_t)(g_last_raw - raw);   /* down-counter; wraps correctly */
    g_last_raw  = raw;

    uint64_t hz = ASSUMED_TIMER_HZ;
    tp->tv_sec  = (time_t)(g_ticks / hz);
    tp->tv_nsec = (long)(((g_ticks % hz) * 1000000000ull) / hz);
    return 0;
}

unsigned nspire_timer_hz(void) { return ASSUMED_TIMER_HZ; }

unsigned nspire_cpu_hz(void) {
    uint32_t main = MMIO32(PMU_CLK_MAIN);
    uint32_t mult = (main >> 24) & 0xFFu, div1 = (main >> 16) & 0x3Fu;
    if (!div1 || !mult) return 0;
    uint32_t hz = (uint32_t)(((uint64_t)12000000u * mult) / div1);
    if (((MMIO32(PMU_CLK_GATE) >> 4) & 1u) && !((main >> 4) & 1u))
        hz /= (((MMIO32(PMU_CLK_DIV2) >> 20) & 0xFu) + 1u);
    return hz * 2u;   /* the PMU formula yields AHB; CPU = 2 x AHB. The screen said 198, not 396. */
}
#endif /* NSPIRE_HOST_TEST */
