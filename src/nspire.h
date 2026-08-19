/* nspire.h -- TI-Nspire porting seam for llama2.c's runq.c.
 *
 * runq.c already has a porting seam: it includes "win.h" on Windows, which supplies mmap, munmap,
 * MAP_FAILED and clock_gettime. We supply exactly the same four things, so the diff to upstream is
 * three lines in the include block plus renaming main(). Following the existing seam rather than
 * inventing one keeps us able to re-apply against a newer runq.c.
 *
 * What is genuinely different here:
 *   - No MMU-backed file mapping. mmap() becomes malloc + read. That makes the 17 MB checkpoint a
 *     real heap allocation rather than a virtual mapping, which is the point -- it is the first
 *     honest test of whether the model fits in an Ndless application's heap.
 *   - No clock_gettime. We drive the SP804 fast timer at 0x90010000, the same one the Phase 0
 *     benchmarks validate, with 64-bit wrap accumulation because a 32-bit counter at ~99 MHz wraps
 *     every ~43 s and a generation run is longer than that.
 */
#ifndef NSPIRE_SEAM_H
#define NSPIRE_SEAM_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#define PROT_NONE    0
#define PROT_READ    1
#define PROT_WRITE   2
#define PROT_EXEC    4
#define MAP_FILE     0
#define MAP_SHARED   1
#define MAP_PRIVATE  2
#define MAP_FAILED   ((void *)-1)

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

void *mmap(void *addr, size_t len, int prot, int flags, int fildes, long off);
int   munmap(void *addr, size_t len);
int   clock_gettime(int clk_id, struct timespec *tp);

/* Reported by our main() so a run's numbers can be audited after the fact. */
unsigned nspire_cpu_hz(void);
unsigned nspire_timer_hz(void);

#endif
