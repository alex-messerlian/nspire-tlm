/* bench_cas.c -- can an Ndless program drive the calculator's own CAS?
 *
 * WHY THIS DECIDES THE ARCHITECTURE. If the OS CAS is callable, the tool layer becomes the CAS the
 * calculator already ships -- a full symbolic engine, for free, in ROM -- and Backend 1
 * (tools/eval/, ~4500 lines) becomes a fallback rather than the product. If it is not callable, we
 * know that and stop asking.
 *
 * WHAT IS ALREADY ESTABLISHED, STATICALLY, WITHOUT A DEVICE:
 *   Both syscalls are MAPPED on this exact OS. In
 *   vendor/Ndless/ndless-sdk/include/syscall-list.h:
 *     #define e_calc_cmd              339   // int TI_MS_evaluateExpr_ACBER(void*, void*, const uint16_t*, void*, void*)
 *     #define e_TI_MS_MathExprToStr   342   // int TI_MS_MathExprToStr(void*, void*, uint16_t**)
 *   and in the OS symbol table for the CAS build of 6.4.0.74
 *   (ndless/src/tools/MakeSyscalls/idc/OS_cascx2-6.4.0.74.idc):
 *     0x100B3138  calc_cmd              <- what 339 resolves through
 *     0x100B2E68  TI_MS_MathExprToStr
 *   Note 339 resolves through the symbol `calc_cmd`, NOT through the TI internal name that appears
 *   in the header comment -- `evaluateExpr` is named in ZERO of the 49 OS tables, on any version or
 *   variant, which would look like absence if you grepped for the wrong string.
 *
 * WHAT IS NOT ESTABLISHED, AND IS THE ENTIRE POINT OF THIS PROBE: the calling convention. p3 is the
 * expression as UTF-16. p1, p2, p4, p5 are unknown, and no caller exists anywhere in the SDK, the
 * samples, or this repo. There is no OS image and no IDA database here to reverse them from, and
 * the two symbols are ALONE in their 8 KB neighbourhood -- no struct, no vtable, no adjacent
 * annotation to infer a shape from. So the convention has to be found empirically, on hardware.
 *
 * WHICH MAKES THE CRASH DESIGN THE IMPORTANT PART.
 *
 * A wrong pointer here is not an exception. There is no MMU protection for an Ndless program: it is
 * a hard reset, and a reset loses anything not already on disk. So:
 *
 *   1. Every attempt is written to the log and FLUSHED BEFORE it is made, never after. A crash then
 *      names the attempt that caused it. An unflushed result is a measurement that never happened.
 *   2. The attempt counter PERSISTS. Relaunching after a reset resumes at the NEXT attempt rather
 *      than repeating the one that just killed the device. That is what converts an unbounded
 *      reversing problem into a bounded sequence of runs: worst case one reset per hypothesis,
 *      and the operator just keeps relaunching.
 *   3. Attempts are ordered SAFEST FIRST -- all-NULL before any pointer, stack before heap -- so the
 *      cheap information is banked before anything risky is attempted.
 *
 * RUN IT ON BATTERY, USB DISCONNECTED, like everything else here. Not because this is a timing
 * measurement, but because a reset while tethered can leave the link in a state that needs a
 * replug, and you will be doing a lot of relaunching.
 */
#include <libndls.h>
#include <syscall-decls.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include "nspire_screen.h"   /* printf -> on-screen console */

/* Both files are resolved at runtime rather than hardcoded, because this program now runs on a
 * SECOND calculator that has none of this project's directories on it.
 *
 * The state file matters more than the log. If it cannot be written, the attempt counter never
 * advances, every relaunch after a reset retries the SAME hypothesis, and the operator sees an
 * infinite reset loop that is indistinguishable from "attempt 0 always resets". The counter is the
 * whole mechanism that makes a nine-hypothesis sweep bounded, so a silent failure to persist it
 * turns a 20-second procedure into an unbounded one. It is written AND READ BACK before any
 * dangerous call is made, and the probe refuses to proceed if that round trip fails. */
static const char *DIRS[] = { "/documents/tlm/", "/documents/bench/", "/documents/ndless/",
                              "/documents/" };
static char LOG_PATH[64], STATE_PATH[64];

static FILE *LOG;

static void say(const char *fmt, ...) {
    va_list ap;
    char buf[256];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    printf("%s\n", buf);
    if (LOG) { fprintf(LOG, "%s\n", buf); fflush(LOG); }   /* flush: a crash must not lose this */
}

/* Find a directory that can hold BOTH files. Verified by an actual write-read-back, not by an
 * fopen that may succeed on a read-only or full filesystem. */
static int resolve_paths(void) {
    for (unsigned i = 0; i < sizeof DIRS / sizeof DIRS[0]; i++) {
        snprintf(STATE_PATH, sizeof STATE_PATH, "%scasnext.txt.tns", DIRS[i]);
        FILE *f = fopen(STATE_PATH, "w");
        if (!f) continue;
        int wrote = fprintf(f, "0\n") > 0;
        fclose(f);
        if (!wrote) continue;
        f = fopen(STATE_PATH, "r");
        if (!f) continue;
        int back = -1, ok = (fscanf(f, "%d", &back) == 1 && back == 0);
        fclose(f);
        if (!ok) continue;
        snprintf(LOG_PATH, sizeof LOG_PATH, "%scaslog.txt.tns", DIRS[i]);
        return 1;
    }
    return 0;
}

/* Which attempt to make on this run. Persisted, so a reset resumes at the next one. */
static int load_next(void) {
    FILE *f = fopen(STATE_PATH, "r");
    if (!f) return 0;
    int n = 0;
    if (fscanf(f, "%d", &n) != 1) n = 0;
    fclose(f);
    return (n < 0 || n > 999) ? 0 : n;
}
/* Returns 1 only if the value is on disk and reads back. The caller must not make a dangerous call
 * unless this succeeded, or a reset would resume at the wrong attempt -- forever. */
static int save_next(int n) {
    FILE *f = fopen(STATE_PATH, "w");
    if (!f) return 0;
    fprintf(f, "%d\n", n);
    fflush(f);
    fclose(f);
    f = fopen(STATE_PATH, "r");
    if (!f) return 0;
    int back = -1;
    int ok = (fscanf(f, "%d", &back) == 1 && back == n);
    fclose(f);
    return ok;
}

/* ---- the attempt table -------------------------------------------------------------------------
 * Ordered safest-first. `desc` is what gets logged BEFORE the call, so a reset identifies it. */
typedef struct { const char *desc; int p1_kind, p2_kind, p4_kind, p5_kind; } attempt;

/* argument kinds: 0 = NULL, 1 = pointer to a zeroed 64-byte stack block, 2 = pointer to an int 0,
 * 3 = pointer to a pointer (out-param shape) */
enum { A_NULL = 0, A_BLOCK, A_INT, A_PPTR };

/* ---- ROUND 2 ------------------------------------------------------------------------------------
 * Round 1 ran all nine hypotheses on hardware with ZERO resets and returned a clear signal:
 *
 *     rc=0    on attempts 4, 8, 9   -- every attempt where p4 was a valid pointer
 *     rc=1020 on attempts 1,2,3,5,6,7 -- every attempt where p4 was NULL
 *
 * So p4 is REQUIRED and is where the result goes. 1020 is this API's invalid-argument code.
 *
 * Round 1 could not finish the job because of a gap in round 1: it dumped b1, b2, i4, i5 and pp2 --
 * and never dumped b4, which is the buffer the successful attempts actually passed. The handle was
 * very likely sitting in it, unprinted. MathExprToStr was then called with NULL handles, returned
 * rc=0 sixteen times, and gave out=0x0 every time -- success at doing nothing.
 *
 * Round 2 dumps every buffer, and then tries the handle arrangements that p4's contents suggest.
 */
static const attempt ATTEMPTS[] = {
    /* Everything NULL but the expression. Many OS entry points return an error code for this rather
     * than dereferencing, so it is the cheapest way to learn the return-value convention. */
    { "all NULL except expr",            A_NULL,  A_NULL,  A_NULL,  A_NULL  },
    /* One out-param at a time, so a crash names which position is dereferenced. */
    { "p1=block",                        A_BLOCK, A_NULL,  A_NULL,  A_NULL  },
    { "p2=block",                        A_NULL,  A_BLOCK, A_NULL,  A_NULL  },
    { "p4=block",                        A_NULL,  A_NULL,  A_BLOCK, A_NULL  },
    { "p5=block",                        A_NULL,  A_NULL,  A_NULL,  A_BLOCK },
    /* The shape this most resembles: a context in, a result handle out. */
    { "p1=block p2=block",               A_BLOCK, A_BLOCK, A_NULL,  A_NULL  },
    { "p1=block p2=pptr",                A_BLOCK, A_PPTR,  A_NULL,  A_NULL  },
    { "p1=block p2=block p4=int p5=int", A_BLOCK, A_BLOCK, A_INT,   A_INT   },
    { "all four = blocks",               A_BLOCK, A_BLOCK, A_BLOCK, A_BLOCK },
};
#define NATTEMPTS ((int)(sizeof ATTEMPTS / sizeof ATTEMPTS[0]))

/* First 32 bytes as words, plus a note if anything is non-zero -- the handle will be a pointer. */
static void dump_block(const char *name, const char *b) {
    const uint32_t *w = (const uint32_t *)(const void *)b;
    int any = 0;
    for (int i = 0; i < 8; i++) if (w[i]) any = 1;
    say("  %s: %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX%s",
        name, (unsigned long)w[0], (unsigned long)w[1], (unsigned long)w[2], (unsigned long)w[3],
        (unsigned long)w[4], (unsigned long)w[5], (unsigned long)w[6], (unsigned long)w[7],
        any ? "   <-- NON-ZERO" : "");
}

/* Given a successful evaluate, try every plausible way of handing the result to MathExprToStr.
 * Each candidate is built only from memory we own or values the OS just wrote there. */
static void try_render(char *b4blk) {
    uint32_t *w = (uint32_t *)(void *)b4blk;
    struct { const char *desc; void *h1; void *h2; } C[] = {
        { "h1=b4          h2=NULL",        (void *)b4blk,        0 },
        { "h1=*(void**)b4 h2=NULL",        (void *)(uintptr_t)w[0], 0 },
        { "h1=NULL        h2=b4",          0,                    (void *)b4blk },
        { "h1=b4          h2=b4+4",        (void *)b4blk,        (void *)(b4blk + 4) },
        { "h1=*(void**)b4 h2=*(void**)b4+4", (void *)(uintptr_t)w[0], (void *)(uintptr_t)w[1] },
        { "h1=&b4         h2=NULL",        (void *)&b4blk,       0 },
    };
    for (unsigned i = 0; i < sizeof C / sizeof C[0]; i++) {
        uint16_t *out = 0;
        say("  render try %u: %s", i + 1, C[i].desc);
        int rc = TI_MS_MathExprToStr(C[i].h1, C[i].h2, &out);
        say("    rc=%d out=%p", rc, (void *)out);
        if (rc == 0 && out) {
            char ascii[128];
            unsigned n = (unsigned)utf16_strlen(out);
            if (n > 120) n = 120;
            utf162ascii(ascii, out, (int)n);
            ascii[n] = 0;
            say("    *** STRING: \"%s\" ***", ascii);
            say("    *** if that reads 2, the OS CAS IS DRIVEABLE. Stop and report this line. ***");
            return;
        }
    }
    say("  no handle arrangement produced a string");
}

int main(void) {
    if (!resolve_paths()) {
        /* No writable directory. The sweep cannot be made bounded without one, so refuse rather
         * than run a procedure that can never terminate. */
        screen_init();
        printf("bench_cas: no writable directory found.\n");
        printf("Tried /documents/{tlm,bench,ndless}/ and /documents/.\n");
        printf("Create one (e.g. copy any file into /documents/tlm/) and re-run.\n");
        printf("\nPress any key.\n");
        wait_key_pressed();
        return 1;
    }
    LOG = fopen(LOG_PATH, "a");

    say("=== bench_cas ===");
    say("os: hwtype=%u subtype=%u", (unsigned)nl_hwtype(), (unsigned)nl_hwsubtype());

    /* ---- Step 1: existence, at RUNTIME. Free, safe, and worth the run on its own. ------------- */
    int has_eval = nl_hassyscall(calc_cmd) ? 1 : 0;
    int has_str  = nl_hassyscall(TI_MS_MathExprToStr) ? 1 : 0;
    int has_a2u  = nl_hassyscall(ascii2utf16) ? 1 : 0;
    int has_u2a  = nl_hassyscall(utf162ascii) ? 1 : 0;
    say("hassyscall calc_cmd(339)          = %d", has_eval);
    say("hassyscall TI_MS_MathExprToStr(342)= %d", has_str);
    say("hassyscall ascii2utf16(13)        = %d", has_a2u);
    say("hassyscall utf162ascii(201)       = %d", has_u2a);

    if (!has_eval || !has_str) {
        /* A clean, decisive negative. The static tables said these are mapped; if the runtime
         * disagrees, the static reading was wrong and that is the finding. */
        say("RESULT: the CAS entry points are NOT available at runtime on this OS.");
        say("        The static table said otherwise -- trust this, not the table.");
        if (LOG) fclose(LOG);
        printf("\nlog: %s\nPress any key.\n", LOG_PATH);
        wait_key_pressed();
        return 0;
    }
    say("RESULT: both CAS entry points ARE live at runtime.");

    if (!has_a2u || !has_u2a) {
        say("STOP: the UTF-16 marshalling syscalls are missing, so the expression cannot be");
        say("      built. That is a separate problem from the CAS itself.");
        if (LOG) fclose(LOG);
        wait_key_pressed();
        return 0;
    }

    /* ---- Step 2: one attempt at the calling convention, then stop. ---------------------------- */
    /* RUN EVERY ATTEMPT THAT SURVIVES, in this one launch.
     *
     * The counter is advanced BEFORE each call, so a reset resumes at the next hypothesis. But an
     * attempt that RETURNS need not cost a relaunch -- the loop simply continues. Worst case is
     * therefore one relaunch per resetting attempt; best case is a single launch that walks all
     * nine. The operator does nothing but relaunch after an actual reset. */
    int idx = load_next();
    if (idx >= NATTEMPTS) {
        say("");
        say("All %d attempts have been made. See the log for which returned and which reset.", NATTEMPTS);
        say("Delete %s to start over.", STATE_PATH);
        if (LOG) fclose(LOG);
        wait_key_pressed();
        return 0;
    }
  for (; idx < NATTEMPTS; idx++) {
    /* Advance the counter BEFORE the call, and CONFIRM it landed. If it did not, a reset would
     * resume at this same attempt and the sweep would never terminate. */
    if (!save_next(idx + 1)) {
        say("STOP: could not persist the attempt counter to %s.", STATE_PATH);
        say("      Without it a reset would repeat this attempt forever. Refusing to continue.");
        break;
    }

    const attempt *a = &ATTEMPTS[idx];
    say("");
    say("--- attempt %d/%d: %s ---", idx + 1, NATTEMPTS, a->desc);
    say("if the calculator resets here, THIS is the attempt that did it.");

    static uint16_t expr[32];
    ascii2utf16(expr, "1+1", 3);
    say("expr marshalled: utf16_strlen=%u", (unsigned)utf16_strlen(expr));

    /* 512 not 64. These are handed to an OS routine whose output size is unknown; if it writes a
     * larger structure than the buffer, the overrun lands in this program's own static memory --
     * still a crash rather than anything durable, but a bigger buffer makes the crash less likely
     * without costing anything. */
    static char b1[512], b2[512], b4[512], b5[512];
    static int  i4, i5;
    static void *pp2;
    memset(b1, 0, sizeof b1); memset(b2, 0, sizeof b2);
    memset(b4, 0, sizeof b4); memset(b5, 0, sizeof b5);
    i4 = i5 = 0; pp2 = 0;

    void *p1 = a->p1_kind == A_BLOCK ? (void *)b1 : 0;
    void *p2 = a->p2_kind == A_BLOCK ? (void *)b2
             : a->p2_kind == A_PPTR  ? (void *)&pp2 : 0;
    void *p4 = a->p4_kind == A_BLOCK ? (void *)b4
             : a->p4_kind == A_INT   ? (void *)&i4 : 0;
    void *p5 = a->p5_kind == A_BLOCK ? (void *)b5
             : a->p5_kind == A_INT   ? (void *)&i5 : 0;

    say("calling TI_MS_evaluateExpr_ACBER(%p, %p, expr, %p, %p)", p1, p2, p4, p5);
    int rc = TI_MS_evaluateExpr_ACBER(p1, p2, expr, p4, p5);
    say("RETURNED rc=%d  (no reset)", rc);

    /* Anything non-zero anywhere is a clue about which argument received output. */
    /* DUMP EVERY BUFFER. Round 1 printed b1 and b2 and not b4 -- and b4 was the only one the
     * successful attempts passed. A buffer you hand to a routine and then do not read is a
     * measurement you did not take. */
    dump_block("b1", b1); dump_block("b2", b2);
    dump_block("b4", b4); dump_block("b5", b5);
    say("after: i4=%d i5=%d pp2=%p", i4, i5, pp2);

    /* ---- Step 3: only if evaluate returned something, try to render it. ---------------------- */
    if (rc == 0) {
        say("rc==0 -- walking handle arrangements for the result");
        try_render(b4);
    } else {
        say("rc=%d (1020 = invalid argument on this API), not rendering", rc);
    }

    say("attempt %d survived.", idx + 1);
  }
    say("");
    say("ALL %d ATTEMPTS COMPLETE in this launch -- none of the remaining ones reset.", NATTEMPTS);
    if (LOG) fclose(LOG);
    printf("\nlog: %s\nPress any key.\n", LOG_PATH);
    wait_key_pressed();
    return 0;
}
