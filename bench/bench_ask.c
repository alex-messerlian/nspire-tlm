/* bench_ask -- WHICH STAGE OF THE ENTER PATH KILLS chattlm?
 *
 * Reported from the device: the app opens, the UI works, sessions can be deleted, and the process
 * VANISHES the instant enter is pressed on a question. That is one symptom with many causes, and
 * device_app.c's own comment names the worst of them: "rq_build() reaches read_checkpoint(), which
 * exit()s ... which looks exactly like the app quitting on send".
 *
 * Reading the code did not settle it, and two hypotheses were already killed by evidence rather
 * than argument: the golden agrees with the host 8/8 on argmax, so the engine is sound, and
 * bench_memceiling reports first_alloc_11417728=ok with after_tok_store_runstate unchanged at
 * 22,377,216, so the model's 11.4 MB allocation is not being squeezed by the larger store.
 *
 * So this walks the SAME stages the app walks, in the SAME order, with a FLUSHED marker printed
 * BEFORE each one. bench_result() fflushes every line, so whichever marker is last in the file is
 * the call that did not return. A crash, an exit() and a hang all leave the same evidence here,
 * which is the whole point: those three are indistinguishable from the screen.
 *
 * It is ~120 lines on purpose. This repo's Phase 2 bring-up cost five device cycles to three bugs
 * that were in the INSTRUMENTATION, and two small reproducers ended it. The standing rule is to
 * build one BEFORE the next round-trip, and to prove the diagnostic can print from the translation
 * unit under test -- which is why g_nspire_log is routed below before any engine call.
 */
#include "common.h"
#include "../src/store/loader.h"
#include "../src/store/assemble.h"
#include "../src/store/askparse.h"
#include "../src/store/tokenizer.h"
#include <stdint.h>
#include <string.h>

extern void  rq_build(const char *path);
extern int   rq_probe(const char *path, char *why, int cap);
extern float *rq_forward(int token, int pos);
extern int   rq_vocab(void);
/* DEFINED HERE, like bench_forward does. The symbol lives in nspire_main.c, which carries its own
 * main() and so cannot be linked into a bench. src/nspire.c and src/runq_nspire.c write their load
 * traces and their out-of-memory report through it, guarded by `if (g_nspire_log)`, so a bench that
 * leaves it NULL silently discards exactly the lines that would explain an exit(). */
FILE *g_nspire_log = 0;

static const char *DIRS[] = { "/documents/tlm/", "/documents/", "/documents/models/" };

/* The questions the operator actually typed, plus one that must take the OTHER branch. */
static const char *Q[] = {
    "what is hookes law",      /* A88 says CONFIDENT -> skips the picker, my new code path */
    "what is kinetic energy",  /* also confident                                            */
    "A sled is pushed 84 m in 7 s at constant speed. Find the speed.",  /* NOT confident    */
};

int main(void) {
    bench_open("bench_ask");
    g_nspire_log = bench_log();      /* BEFORE any engine call, or its traces are discarded */
    bench_result("log_routed", "%s", g_nspire_log ? "yes" : "NO -- engine will be silent");

    static char base[64], mpath[96];
    static ns_store2 ST;
    static ns_tok TK;

    /* ---- 1. the store ------------------------------------------------------------------- */
    int loaded = 0;
    for (unsigned i = 0; i < sizeof DIRS / sizeof DIRS[0] && !loaded; i++) {
        char p[96];
        snprintf(p, sizeof p, "%sstore.tns.tns", DIRS[i]);
        bench_result("stage", "ns_load: trying %s", p);
        if (ns_load(&ST, p) == NS_OK) {
            snprintf(base, sizeof base, "%s", DIRS[i]);
            loaded = 1;
        }
    }
    if (!loaded) { bench_result("RESULT", "%s", "ABORTED -- no store.tns.tns found"); bench_close(); return 1; }
    bench_result("store_records", "%d", ST.n);

    /* ---- 2. the tokenizer --------------------------------------------------------------- */
    { char p[96]; snprintf(p, sizeof p, "%stok4096.tok.tns", base);
      bench_result("stage", "ns_tok_load: %s", p);
      if (ns_tok_load(&TK, p) != NST_OK) {
          bench_result("RESULT", "%s", "ABORTED -- tokenizer failed to load"); bench_close(); return 1; } }
    bench_result("stage", "%s", "ns_tok_load: returned");

    /* ---- 3. the per-question stages, which is where the app dies ------------------------- */
    for (unsigned i = 0; i < sizeof Q / sizeof Q[0]; i++) {
        bench_result("question", "[%u] %s", i, Q[i]);

        static ns_ask a;
        bench_result("stage", "[%u] ask_parse: entering", i);
        ask_parse(Q[i], &a);
        bench_result("stage", "[%u] ask_parse: returned, nvals=%d", i, a.in.nvals);

        /* THE A88 CODE. Newly on this path and never run on hardware before today. */
        int idx = -1;
        bench_result("stage", "[%u] ask_confident: entering", i);
        int conf = ask_confident(&ST, Q[i], &a.in, &idx);
        bench_result("stage", "[%u] ask_confident: returned conf=%d idx=%d", i, conf, idx);
        if (idx >= 0) {
            bench_result("stage", "[%u] qcover: entering", i);
            int qc = ask_qcover(&ST, idx, Q[i]);
            bench_result("stage", "[%u] qcover=%d%% name=%s", i, qc,
                         ST.rec[idx].name ? ST.rec[idx].name : "(none)");
            bench_result("stage", "[%u] rid=%s", i, ST.rec[idx].rid ? ST.rec[idx].rid : "(NULL)");
        }

        /* ---- 4. assemble, exactly as app_request does ----------------------------------- */
        if (idx >= 0) {
            static char prompt[NS_PROMPT_MAX];
            bench_result("stage", "[%u] ns_assemble: entering", i);
            int n = ns_assemble(prompt, sizeof prompt, &ST.rec[idx], Q[i], &a.in);
            bench_result("stage", "[%u] ns_assemble: returned %d", i, n);
            if (n > 0) {
                static int ids[NS_MAX_TOKENS];
                bench_result("stage", "[%u] ns_tok_encode: entering", i);
                int t = ns_tok_encode(&TK, prompt, ids, NS_MAX_TOKENS);
                bench_result("stage", "[%u] ns_tok_encode: returned %d tokens", i, t);
            }
        }
    }

    /* ---- 5. the model, last, because it is the one that exit()s -------------------------- */
    snprintf(mpath, sizeof mpath, "%smodel4096.bin.tns", base);
    { char why[128] = { 0 };
      bench_result("stage", "rq_probe: %s", mpath);
      int bad = rq_probe(mpath, why, sizeof why);
      bench_result("probe", "%s", bad ? why : "ok");
      if (bad) { bench_result("RESULT", "%s", "ABORTED -- checkpoint unloadable"); bench_close(); return 1; } }

    bench_result("stage", "%s", "rq_build: entering read_checkpoint + malloc_run_state");
    rq_build(mpath);
    bench_result("stage", "%s", "rq_build: returned");
    bench_result("stage", "%s", "rq_forward(1,0): entering");
    rq_forward(1, 0);
    bench_result("stage", "%s", "rq_forward(1,0): returned");

    bench_result("RESULT", "%s", "ALL STAGES COMPLETED -- the Enter path is clean in isolation, "
                                 "so the fault is in the UI layer above it, not in these calls.");
    g_nspire_log = 0;
    bench_close();
    return 0;
}
