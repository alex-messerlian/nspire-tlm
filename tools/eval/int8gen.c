/* THE CALCULATOR'S GENERATION LOOP, ON THE HOST: int8 engine, tool injection, runtime check.
 *
 * usage: int8gen <model.bin> <tok4096.tok>   < prompts, one per line
 * prints one line per prompt:
 *   GEN<TAB>emitted text<TAB>result<TAB>prose_states_result<TAB>space-separated emitted ids
 *
 * The loop is src/store/gencore.c -- the SAME function the calculator app and the device benchmark
 * call -- run over the same runq_nspire.c, tokenizer and toolrun.c. The post-answer check is the
 * shipped answer_states_result, lifted verbatim by sed (build/ansmatch_impl.h). What this omits is
 * presentation (screen, keypad) and earlier conversation turns, which an eval item does not have.
 * The trailing ids let tools/eval/parity_toolloop.py compare a device-logged turn id for id.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tokenizer.h"
#include "toolrun.h"
#include "gencore.h"
#include "eval.h"

#define main runq_main_unused
#include "../../src/runq_nspire.c"
#undef main

#include "ansmatch_impl.h"

static void put_escaped(const char *s) {
    for (; *s; s++) {
        if (*s == '\n') fputs("\\n", stdout);
        else if (*s == '\t') fputs("\\t", stdout);
        else if (*s == '\\') fputs("\\\\", stdout);
        else putchar(*s);
    }
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: int8gen <model.bin> <tok>\n"); return 2; }
    static ns_tok TK;
    if (ns_tok_load(&TK, argv[2]) != NST_OK) { fprintf(stderr, "tokenizer: %s\n", argv[2]); return 2; }
    rq_build(argv[1]);

    static char prompt[4096];
    while (fgets(prompt, sizeof prompt, stdin)) {
        prompt[strcspn(prompt, "\r\n")] = 0;
        static int ids[1024];
        int n = ns_tok_encode(&TK, prompt, ids, 1024);
        if (n <= 0) { puts("GEN\t!encode\t\t0"); fflush(stdout); continue; }

        static tlm_gen_result R;
        tlm_generate(&TK, ids, n, 0, &R);
        const int *emitted = R.emitted; int nemit = R.nemit;
        int tool_ok = R.tool_ok; const char *tool_res = R.tool_res;
        static char full[4096];
        ns_tok_decode(&TK, emitted, nemit, full, sizeof full);
        int states = 1;                       /* no result to check: nothing would be appended */
        if (tool_ok && tool_res[0] && tool_res[0] != '!') {
            const char *a = strstr(full, "<a>");
            states = a ? answer_states_result(a + 3, tool_res) : 0;
        }
        fputs("GEN\t", stdout); put_escaped(full);
        printf("\t%s\t%d\t", tool_res, states);
        for (int i = 0; i < nemit; i++) printf(i ? " %d" : "%d", emitted[i]);
        putchar('\n');
        fflush(stdout);
    }
    return 0;
}
