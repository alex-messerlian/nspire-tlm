/* THE CALCULATOR'S GENERATION LOOP, ON THE HOST: int8 engine, tool injection, runtime check.
 *
 * usage: int8gen <model.bin> <tok4096.tok>   < prompts, one per line
 * prints one line per prompt:  GEN<TAB>emitted text<TAB>result<TAB>prose_states_result
 *
 * Why this exists. score_correct.py scores the fp32 PyTorch model; the calculator runs the int8 C
 * engine, and tools/eval/int8_calls.py measured that the two write different first calls on 5.8%
 * of answer_0 items. The number a paper can attach to "the calculator" is the one this binary
 * produces: the same runq_nspire.c, the same tokenizer, the same toolrun.c, and the loop below
 * copied from app_request in src/store/device_app.c --
 *   - prefill every prompt token but the last, decode from the last;
 *   - at most 90 generated steps and pos < 250;
 *   - the <res> logit suppressed before the argmax;
 *   - on </tool>: decode everything emitted, tlm_extract_call, tlm_result_span (or <res>!give</res>
 *     for a malformed span), encode the injection and feed it with the device's own
 *     consume-then-replace step;
 *   - the post-answer check is the shipped answer_states_result, lifted verbatim by sed
 *     (build/ansmatch_impl.h), so "the runtime would append a correction" is decided by the
 *     function the calculator runs.
 * What it omits: the screen, the keypad, and earlier conversation turns (an eval item has none).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tokenizer.h"
#include "toolrun.h"
#include "eval.h"

#define main runq_main_unused
#include "../../src/runq_nspire.c"
#undef main

#include "ansmatch_impl.h"

static int argmax_of(const float *v, int n) {
    int b = 0;
    for (int i = 1; i < n; i++) if (v[i] > v[b]) b = i;
    return b;
}

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
    const int V = rq_vocab();
    const int ID_RES = ns_tok_special_id(&TK, "<res>");
    const int ID_TOOLC = ns_tok_special_id(&TK, "</tool>");
    const int ID_END = ns_tok_special_id(&TK, "<end>");

    static char prompt[4096];
    while (fgets(prompt, sizeof prompt, stdin)) {
        prompt[strcspn(prompt, "\r\n")] = 0;
        static int ids[1024];
        int n = ns_tok_encode(&TK, prompt, ids, 1024);
        if (n <= 0) { puts("GEN\t!encode\t\t0"); fflush(stdout); continue; }

        int tok = ids[0], pos = 0;
        while (pos < n - 1) { rq_forward(tok, pos); pos++; tok = ids[pos]; }

        static int emitted[300];
        int nemit = 0, tool_ok = 0;
        static char tool_res[48];
        tool_res[0] = 0;
        for (int s = 0; s < 90 && pos < 250; s++) {
            float *lg = rq_forward(tok, pos); pos++;
            if (ID_RES >= 0) lg[ID_RES] = -1e30f;
            tok = argmax_of(lg, V);
            if (nemit < (int)(sizeof emitted / sizeof emitted[0])) emitted[nemit++] = tok;
            if (tok == ID_END || tok == 10) break;
            if (tok == ID_TOOLC) {
                static char doc[1024], span[320], inj[MAX_RESULT + 16];
                ns_tok_decode(&TK, emitted, nemit, doc, sizeof doc);
                if (tlm_extract_call(doc, span, sizeof span)) {
                    tool_ok = tlm_result_span(span, inj, sizeof inj);
                    const char *o = strstr(inj, "<res>");
                    const char *c = o ? strstr(o, "</res>") : 0;
                    if (o && c) {
                        int L = (int)(c - o) - 5;
                        if (L >= (int)sizeof tool_res) L = (int)sizeof tool_res - 1;
                        if (L > 0) { memcpy(tool_res, o + 5, (size_t)L); tool_res[L] = 0; }
                    }
                } else
                    snprintf(inj, sizeof inj, "<res>!give</res>");
                int iids[64];
                int ni = ns_tok_encode(&TK, inj, iids, 64);
                for (int k = 0; k < ni && pos < 250; k++) {
                    rq_forward(tok, pos); pos++;
                    tok = iids[k];
                    if (nemit < (int)(sizeof emitted / sizeof emitted[0])) emitted[nemit++] = tok;
                }
            }
        }
        static char full[4096];
        ns_tok_decode(&TK, emitted, nemit, full, sizeof full);
        int states = 1;                       /* no result to check: nothing would be appended */
        if (tool_ok && tool_res[0] && tool_res[0] != '!') {
            const char *a = strstr(full, "<a>");
            states = a ? answer_states_result(a + 3, tool_res) : 0;
        }
        fputs("GEN\t", stdout); put_escaped(full);
        printf("\t%s\t%d\n", tool_res, states);
        fflush(stdout);
    }
    return 0;
}
