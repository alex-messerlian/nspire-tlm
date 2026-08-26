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

#define LOG_PATH   "/documents/tlm/caslog.txt.tns"
#define STATE_PATH "/documents/tlm/casnext.txt.tns"

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

/* Which attempt to make on this run. Persisted, so a reset resumes at the next one. */
static int load_next(void) {
    FILE *f = fopen(STATE_PATH, "r");
    if (!f) return 0;
    int n = 0;
    if (fscanf(f, "%d", &n) != 1) n = 0;
    fclose(f);
    return (n < 0 || n > 999) ? 0 : n;
}
static void save_next(int n) {
    FILE *f = fopen(STATE_PATH, "w");
    if (!f) return;
    fprintf(f, "%d\n", n);
    fflush(f);
    fclose(f);
}

/* ---- the attempt table -------------------------------------------------------------------------
 * Ordered safest-first. `desc` is what gets logged BEFORE the call, so a reset identifies it. */
typedef struct { const char *desc; int p1_kind, p2_kind, p4_kind, p5_kind; } attempt;

/* argument kinds: 0 = NULL, 1 = pointer to a zeroed 64-byte stack block, 2 = pointer to an int 0,
 * 3 = pointer to a pointer (out-param shape) */
enum { A_NULL = 0, A_BLOCK, A_INT, A_PPTR };

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

int main(void) {
    LOG = fopen(LOG_PATH, "a");
    if (!LOG) LOG = fopen("/documents/bench/caslog.txt.tns", "a");

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
    /* Advance the counter BEFORE the call. If this attempt resets the device, the next launch
     * moves on instead of reproducing the reset forever. */
    save_next(idx + 1);

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
    say("after: b1[0..7]=%02X %02X %02X %02X %02X %02X %02X %02X",
        (unsigned char)b1[0],(unsigned char)b1[1],(unsigned char)b1[2],(unsigned char)b1[3],
        (unsigned char)b1[4],(unsigned char)b1[5],(unsigned char)b1[6],(unsigned char)b1[7]);
    say("after: b2[0..7]=%02X %02X %02X %02X %02X %02X %02X %02X",
        (unsigned char)b2[0],(unsigned char)b2[1],(unsigned char)b2[2],(unsigned char)b2[3],
        (unsigned char)b2[4],(unsigned char)b2[5],(unsigned char)b2[6],(unsigned char)b2[7]);
    say("after: i4=%d i5=%d pp2=%p", i4, i5, pp2);

    /* ---- Step 3: only if evaluate returned something, try to render it. ---------------------- */
    if (rc == 0) {
        say("rc==0, attempting TI_MS_MathExprToStr on the same buffers");
        uint16_t *out = 0;
        int rc2 = TI_MS_MathExprToStr(p1, p2, &out);
        say("MathExprToStr rc=%d out=%p", rc2, (void *)out);
        if (out) {
            char ascii[128];
            unsigned n = (unsigned)utf16_strlen(out);
            if (n > 120) n = 120;
            utf162ascii(ascii, out, (int)n);
            ascii[n] = 0;
            say("RESULT STRING: \"%s\"", ascii);
            say("*** if that reads 2, the OS CAS is driveable and the architecture changes. ***");
        }
    } else {
        say("rc!=0, not attempting the string conversion");
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
