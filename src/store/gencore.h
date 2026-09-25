/* The generation loop, in ONE place. See gencore.c for why. */
#ifndef TLM_GENCORE_H
#define TLM_GENCORE_H

#include "tokenizer.h"

#define TLM_GEN_STEPS   90      /* generated steps, as the app has always capped them */
#define TLM_GEN_POSMAX  250     /* position cap, likewise */
#define TLM_GEN_MAXEMIT 300     /* emitted + injected ids kept for span extraction */

/* Every hook may be NULL. `ctx` is passed back unchanged. */
typedef struct {
    void *ctx;
    void (*on_prefill)(void *ctx, int done, int total);     /* every 8 prompt tokens and the last */
    void (*on_token)(void *ctx, int tok);                   /* each token the MODEL chose */
    void (*on_tool_begin)(void *ctx, const char *span);     /* a call closed; about to execute */
    void (*on_tool_end)(void *ctx, int ok, const char *res, /* executed, before injection; `doc` is */
                        const char *span, const char *doc); /* everything emitted so far, decoded   */
    void (*on_inject)(void *ctx, int tok);                  /* each token of the injected <res> span */
    void (*on_injected)(void *ctx);                         /* after the whole injection */
    int  (*should_stop)(void *ctx, int in_prefill);         /* polled; nonzero stops the turn */
} tlm_gen_hooks;

typedef struct {
    int  emitted[TLM_GEN_MAXEMIT];   /* model tokens and injected tokens, in order */
    int  nemit;
    int  tool_ok;                    /* the LAST executed call returned a value */
    char tool_res[48];               /* its result text, "" if none */
    int  ncalls;                     /* calls executed */
    int  stopped, stopped_in_prefill;
    int  pos;                        /* position after the last forward */
} tlm_gen_result;

/* Prefill ids[0..n-2], then decode greedily from ids[n-1]: suppress <res>, execute each closed
 * <tool> span with toolrun.c and feed its <res> back, stop at <end>, the step cap or the position
 * cap. The model must already be built (rq_build). Returns 0, or -1 for bad arguments. */
int tlm_generate(const ns_tok *tk, const int *ids, int n, const tlm_gen_hooks *h,
                 tlm_gen_result *r);

#endif
