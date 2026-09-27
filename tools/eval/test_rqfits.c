/* test_rqfits -- rq_fits must hold everything build_transformer allocates, at once.
 *
 * The app used to check that the heap could hold the checkpoint alone. On the calculator the heap
 * then failed in malloc_run_state, whose exit() leaves the 11.6 MB checkpoint allocated; Ndless
 * never reclaims the heap of a program that exits, so every later launch said "not enough free
 * RAM" until the reset button was pressed (2026-09-27). rq_fits asks for the checkpoint and both
 * KV-cache blocks together, which is what the load will ask for.
 *
 * The boundary is tested exactly, against the shipped model file, with a malloc that fails past a
 * byte budget: room for the checkpoint and ONE cache block must not be enough (that is the old
 * check's blind spot), one byte short of the need must not be enough, and the need itself must be.
 * Exit 0 all pass, 1 any fail, 2 cannot check (the model file is missing). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static long BUDGET = -1, USED = 0;
static int CALLS = 0;
static void *budget_malloc(size_t n) {
    CALLS++;
    if (BUDGET >= 0 && USED + (long)n > BUDGET) return NULL;
    USED += (long)n;
    return malloc(n);
}
#define malloc budget_malloc
#define main runq_main_unused
#include "../../src/runq_nspire.c"
#undef main
#undef malloc

/* The shipped model's own numbers: width 352, 6 layers, 8 heads and 8 KV heads, context 512, fp32
 * KV cache, so each cache block is 6 * 512 * 352 * 4 bytes. */
static const char *REAL = "build/transfer/model4096.bin.tns";
static const long HALF = 6L * 512 * 352 * 4;

static int fails = 0;
static void expect(int cond, const char *what) {
    printf("  %s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) fails++;
}
static int fits_with(const char *path, long budget, long *need) {
    BUDGET = budget; USED = 0; CALLS = 0;
    return rq_fits(path, need);
}

int main(void) {
    FILE *f = fopen(REAL, "rb");
    if (!f) { printf("test_rqfits CANNOT CHECK: %s is missing\n", REAL); return 2; }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fclose(f);

    long need = 0;
    expect(fits_with(REAL, -1, &need) == 1, "fits with no limit");
    expect(need == fsz + 2 * HALF, "need = checkpoint + both KV-cache blocks");
    expect(CALLS == 3, "asks for three blocks: checkpoint, keys, values");
    expect(fits_with(REAL, need, NULL) == 1, "fits when exactly the need is free");
    expect(fits_with(REAL, need - 1, NULL) == 0, "does not fit one byte short");
    expect(fits_with(REAL, fsz + HALF, NULL) == 0, "does not fit with room for the checkpoint and one block only");
    expect(fits_with(REAL, fsz, NULL) == 0, "does not fit with room for the checkpoint alone (the old check)");
    expect(fits_with("build/no-such-model.bin", -1, NULL) == 0, "a missing file does not fit");

    char tmp[] = "/tmp/test_rqfits_XXXXXX";
    int fd = mkstemp(tmp);
    if (fd >= 0) {
        FILE *t = fdopen(fd, "wb");
        fwrite("ak42", 1, 4, t);                      /* a magic number and nothing else */
        fclose(t);
        expect(fits_with(tmp, -1, NULL) == 0, "a file with a truncated header does not fit");
        remove(tmp);
    } else {
        expect(0, "could not create a temporary file");
    }

    printf("test_rqfits %s  (%d failure(s))\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
