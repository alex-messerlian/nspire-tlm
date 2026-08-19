/* main.c -- host CLI for Backend 1.
 *
 * Two modes:
 *   evalcli '<tool>solve<arg>x^2-4=0<arg>x</tool>'   -- one call, prints the result span
 *   evalcli -                                        -- one call per stdin line, same output
 *
 * The pipe mode is what data generation uses: every tool call in a generated document is executed
 * and verified here before the sample is allowed into the corpus (TOOL_SPEC.md section 8).
 */
#include "eval.h"
#include <stdio.h>
#include <string.h>

static int run_one(const char *line) {
    char out[MAX_RESULT];
    tb_status st = tool_call_text(line, out, sizeof out);
    if (st == TB_ERR) { printf("<res>!parse</res>\n"); return 1; }
    printf("<res>%s</res>\n", out);
    return out[0] == '!';
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "-") == 0) {
        char line[MAX_CALL_BYTES + 2];
        int bad = 0;
        while (fgets(line, sizeof line, stdin)) {
            size_t n = strlen(line);
            while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
            if (!n) continue;
            bad += run_one(line);
        }
        return bad ? 1 : 0;
    }
    if (argc != 2) {
        fprintf(stderr, "usage: %s '<tool>NAME<arg>ARG...</tool>'\n       %s -   (one call per stdin line)\n",
                argv[0], argv[0]);
        return 2;
    }
    return run_one(argv[1]);
}
