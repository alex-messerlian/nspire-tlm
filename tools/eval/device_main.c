/* device_main.c -- run the Backend 1 acceptance suite ON the calculator.
 *
 * This is the cheapest high-value experiment in the whole project. It validates, with a ~500 line
 * program instead of an inference engine:
 *
 *   - the Ndless toolchain end to end (compile, link, genzehn, make-prg, load, run, exit)
 *   - newlib's strtod and snprintf, which are where a host/device determinism break would hide
 *   - soft-float double arithmetic on a core with no FPU
 *   - that TOOL_SPEC section 5.1 formatting really is byte-identical across two libc implementations
 *
 * It gives us a working correctness oracle on hardware BEFORE the model exists. If determinism
 * breaks under newlib we find out now, when the fix is cheap, rather than after a training run.
 *
 * Output goes to a file as well as the screen, because this must be run on battery with USB
 * disconnected -- same constraint as the Phase 0 benchmarks, same reason.
 *
 * PASS CRITERION: byte-identical to the host suite. Not "close". Identical.
 */
#include "eval.h"
#include <libndls.h>
#include <stdio.h>
#include <string.h>

#define DEV_LOG "/documents/bench/eval_device.txt.tns"

static FILE *g_log;
static int pass_n, fail_n;

/* Same signature and semantics as the host harness, so the two suites stay in lockstep. */
static void T(const char *call, const char *want) {
    char got[MAX_RESULT];
    tb_status st = tool_call_text(call, got, sizeof got);
    if (st == TB_ERR) strcpy(got, "<TB_ERR>");
    if (strcmp(got, want) == 0) {
        pass_n++;
    } else {
        fail_n++;
        printf("FAIL %s\n  want=%s\n  got =%s\n", call, want, got);
        if (g_log) fprintf(g_log, "FAIL\t%s\twant=%s\tgot=%s\n", call, want, got);
    }
}

#include "tests.inc"

int main(void) {
    g_log = fopen(DEV_LOG, "w");
    printf("Backend 1 acceptance suite, on device\n");
    if (g_log) fprintf(g_log, "# Backend 1 acceptance suite, on device\n");

    run_tests();

    printf("\n%d passed, %d failed\n", pass_n, fail_n);
    if (g_log) {
        fprintf(g_log, "PASS=%d FAIL=%d\n", pass_n, fail_n);
        fprintf(g_log, "%s\n", fail_n ? "RESULT=DIVERGENT" : "RESULT=IDENTICAL-TO-HOST");
        fclose(g_log);
    }
    printf("log: %s\nPress any key.\n", DEV_LOG);
    wait_key_pressed();
    return 0;
}
