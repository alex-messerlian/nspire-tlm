/* nspire.c -- implementation of the porting seam. See nspire.h for why each piece exists. */
#include "nspire.h"
#undef mmap
#undef munmap
#undef clock_gettime
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/* ---- mmap: malloc + read ------------------------------------------------------------------- */
/* runq.c mmaps the checkpoint once and munmaps it once, so a single-slot record is enough. Keeping
 * the pointer lets munmap() free the right thing without the caller changing. */
static void  *g_map_ptr  = NULL;
static size_t g_map_len  = 0;

void *nspire_mmap(void *addr, size_t len, int prot, int flags, int fildes, long off) {
    (void)addr; (void)prot; (void)flags;
    void *p = malloc(len);
    if (!p) {
        /* This is the failure we most expect on a 64 MB device, so say so plainly rather than
         * returning MAP_FAILED and letting runq.c print "mmap failed". */
        fprintf(stderr, "out of memory: needed %lu bytes for the checkpoint\n", (unsigned long)len);
        return MAP_FAILED;
    }
    if (lseek(fildes, off, SEEK_SET) < 0) { free(p); return MAP_FAILED; }

    /* Read in chunks: a single 17 MB read through newlib is a good way to find out the hard way
     * that some layer uses a 16-bit count somewhere. */
    unsigned char *q = (unsigned char *)p;
    size_t remaining = len;
    while (remaining) {
        size_t want = remaining > (1u << 20) ? (1u << 20) : remaining;
        long got = read(fildes, q, want);
        if (got <= 0) { free(p); return MAP_FAILED; }
        q += got;
        remaining -= (size_t)got;
    }
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
    return hz;
}
#endif /* NSPIRE_HOST_TEST */
