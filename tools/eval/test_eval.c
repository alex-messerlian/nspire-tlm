/* test_eval.c -- host acceptance tests for Backend 1.
 *
 * These are the contract. If the device build does not reproduce this file's output byte for byte,
 * the port is wrong and no training data generated against it is valid. The test bodies live in
 * tests.inc so device_main.c runs the identical list on the calculator.
 */
#include "eval.h"
#include <stdio.h>
#include <string.h>

static int pass_n = 0, fail_n = 0;

static void T(const char *call, const char *want) {
    char got[MAX_RESULT];
    tb_status st = tool_call_text(call, got, sizeof got);
    if (st == TB_ERR) strcpy(got, "<TB_ERR>");
    if (strcmp(got, want) == 0) { pass_n++; }
    else { fail_n++; printf("FAIL  %-52s want=%-22s got=%s\n", call, want, got); }
}

#include "tests.inc"

int main(void) {
    run_tests();
    printf("\n%d passed, %d failed\n", pass_n, fail_n);
    return fail_n ? 1 : 0;
}
