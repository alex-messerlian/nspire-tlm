/* THE GENERATION LOOP, ONE COPY.
 *
 * It lived inline in device_app.c's app_request, and tools/eval/int8gen.c carried a hand copy so
 * the host could score the calculator's decoder. A copy cannot give evidence that the DEVICE runs
 * the tool-execution loop the way the host harness does. Comparing two copies proves only that the copies agree. So the loop is here, and the app,
 * the device benchmark (device_generate.c) and the host harness all call it; a device/host
 * comparison of full tool-using generations is then a comparison of one implementation on two
 * machines -- the same argument that makes the engine parity mean something.
 *
 * Behaviour is exactly app_request's, including its quirks, because the app is what ships:
 *   - prefill every prompt token but the last; hooks every 8 tokens and at the last;
 *   - decode at most TLM_GEN_STEPS steps and to position TLM_GEN_POSMAX;
 *   - the <res> logit is suppressed before the argmax -- the runtime, not the model, emits results;
 *   - on </tool>: decode everything emitted, extract the call, execute it (or inject !give for a
 *     malformed span), and feed the injected ids with the consume-then-replace step;
 *   - stop at <end> (looked up by text, and id 10 as before), and poll should_stop after each step.
 * Everything that is presentation -- streaming, the status line, the correction note appended when
 * the prose misstates the result -- stays with the caller.
 */
#include <stdio.h>
#include <string.h>
#include "gencore.h"
#include "toolrun.h"
#include "../../tools/eval/eval.h"

extern float *rq_forward(int token, int pos);
extern int rq_vocab(void);

static int argmax_v(const float *v, int n) {
    int b = 0;
    for (int i = 1; i < n; i++) if (v[i] > v[b]) b = i;
    return b;
}

int tlm_generate(const ns_tok *tk, const int *ids, int n, const tlm_gen_hooks *h,
                 tlm_gen_result *r) {
    if (!tk || !ids || n <= 0 || !r) return -1;
    memset(r, 0, sizeof *r);
    const int ID_RES = ns_tok_special_id(tk, "<res>");
    const int ID_TOOLC = ns_tok_special_id(tk, "</tool>");
    const int ID_END = ns_tok_special_id(tk, "<end>");
    const int V = rq_vocab();

    int tok = ids[0], pos = 0;
    while (pos < n - 1) {
        rq_forward(tok, pos); pos++; tok = ids[pos];
        if ((pos & 7) == 0 || pos == n - 1) {
            if (h && h->on_prefill) h->on_prefill(h->ctx, pos, n - 1);
            if (h && h->should_stop && h->should_stop(h->ctx, 1)) {
                r->stopped = r->stopped_in_prefill = 1; r->pos = pos; return 0;
            }
        }
    }

    for (int s = 0; s < TLM_GEN_STEPS && pos < TLM_GEN_POSMAX; s++) {
        float *lg = rq_forward(tok, pos); pos++;
        if (ID_RES >= 0) lg[ID_RES] = -1e30f;
        tok = argmax_v(lg, V);
        if (r->nemit < TLM_GEN_MAXEMIT) r->emitted[r->nemit++] = tok;
        if (h && h->on_token) h->on_token(h->ctx, tok);
        if (tok == ID_END || tok == 10) break;

        if (tok == ID_TOOLC) {
            static char doc[1024], span[320], inj[MAX_RESULT + 16];
            ns_tok_decode(tk, r->emitted, r->nemit, doc, sizeof doc);
            if (tlm_extract_call(doc, span, sizeof span)) {
                if (h && h->on_tool_begin) h->on_tool_begin(h->ctx, span);
                r->tool_ok = tlm_result_span(span, inj, sizeof inj);
                r->ncalls++;
                r->tool_res[0] = 0;
                const char *o = strstr(inj, "<res>");
                const char *c = o ? strstr(o, "</res>") : 0;
                if (o && c) {
                    int L = (int)(c - o) - 5;
                    if (L >= (int)sizeof r->tool_res) L = (int)sizeof r->tool_res - 1;
                    if (L > 0) { memcpy(r->tool_res, o + 5, (size_t)L); r->tool_res[L] = 0; }
                }
                if (h && h->on_tool_end) h->on_tool_end(h->ctx, r->tool_ok, r->tool_res, span, doc);
            } else
                snprintf(inj, sizeof inj, "<res>!give</res>");

            int iids[64];
            int ni = ns_tok_encode(tk, inj, iids, 64);
            for (int k = 0; k < ni && pos < TLM_GEN_POSMAX; k++) {
                rq_forward(tok, pos); pos++;             /* consume the token just produced */
                tok = iids[k];
                if (r->nemit < TLM_GEN_MAXEMIT) r->emitted[r->nemit++] = tok;
                if (h && h->on_inject) h->on_inject(h->ctx, tok);
            }
            if (h && h->on_injected) h->on_injected(h->ctx);
        }

        if (h && h->should_stop && h->should_stop(h->ctx, 0)) { r->stopped = 1; break; }
    }
    r->pos = pos;
    return 0;
}
