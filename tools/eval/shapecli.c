/* CLI wrapper so shapecheck.c is reachable from BOTH Python scoring paths.
 *
 * docs/WIRING_AUDIT.md records that provenance.c existed, was unit-tested, had no caller, and read
 * as coverage for months -- and that TWO SEPARATE GRADERS exist here (tools/eval/grade.py for the
 * eval set, train/select_run.py for selection), so a check added once covers half the surface.
 *
 * Deliberately THIN. The document parsing lives in tlm_shape_check_doc(), in the file the DEVICE
 * links, so all three callers run the same code rather than two of them running a copy.
 *
 * Reads one full document on stdin -- prompt + generation, exactly what provcli takes -- and prints
 *     shape=ok | shape=mismatch | shape=unchecked   why=<text>
 */
#include <stdio.h>
#include "shapecheck.h"

int main(void) {
    static char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf - 1, stdin);
    buf[n] = '\0';
    char why[256];
    int st = tlm_shape_check_doc(buf, why, sizeof why);
    printf("shape=%s why=%s\n",
           st == TLM_SHAPE_OK ? "ok" : st == TLM_SHAPE_MISMATCH ? "mismatch" : "unchecked", why);
    return 0;
}
