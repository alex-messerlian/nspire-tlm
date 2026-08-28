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
#include "common.h"         /* timer_acquire/timer_raw/timer_delta, TIMER_FAST_BASE */

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
static int TIMEPATH_OK;

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
        /* casnext3, not casnext. The device's casnext.txt reads 9 -- every round-2 attempt has
         * been made -- so a round-3 binary reusing that name would print "all attempts complete"
         * and exit without running anything. A new attempt TABLE needs a new counter; reusing the
         * name would make the operator's first round-3 run a silent no-op. */
        snprintf(STATE_PATH, sizeof STATE_PATH, "%scasnext3.txt.tns", DIRS[i]);
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
        snprintf(LOG_PATH, sizeof LOG_PATH, "%scaslog.txt.tns", DIRS[i]);   /* appends; rounds 1-2 are above */
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

/* ---- ROUND 3 ------------------------------------------------------------------------------------
 * Round 2 got FURTHER than its own log admits, and the reason is an instrumentation choice.
 *
 * Reproduced identically in THREE independent launches (caslog rounds 9, 10, 11):
 *   * p4 required, p1/p2/p5 optional -- rc=1020 on all six attempts with p4 NULL, rc=0 on all three
 *     with p4 non-NULL. Perfect separation, replicated 3x.
 *   * b4[0] receives a fresh heap handle each launch: 0x11628740 / 0x116377B8 / 0x116348D8.
 *   * MathExprToStr(*(void**)b4, NULL, &out) returns rc=0 AND a NON-NULL out, in a DIFFERENT heap
 *     region: 0x117abb60 / 0x117ac190 / 0x1179ea08. A separate allocation. The chain works.
 *   * The decoded string is "" every time.
 *
 * So the call chain succeeds end to end and yields an allocated, empty-looking string. Round 2
 * CANNOT tell us why, because of three gaps in its own instrument -- not in the OS:
 *
 *   GAP 1  It printed the DECODED ascii and never the RAW BYTES at `out`. utf16_strlen() returning
 *          0 and a genuinely empty result are the same output. If the OS hands back UTF-16 BIG
 *          endian, '2' is 00 32 and a length walk stops on the first byte -- reading as empty while
 *          the data is right there.
 *   GAP 2  try_render returned on the first `rc==0 && out`, so arrangements 3-6 were never tried on
 *          attempts 4 and 9. "First non-NULL pointer" was the wrong stopping condition; the right
 *          one is "first non-EMPTY string".
 *   GAP 3  Attempt 8 passed p4 as &int4 and the handle landed in i4 (291722704 = 0x116355D0, the
 *          same heap region as the others). try_render was then called with b4, which was all
 *          zeroes. That attempt's handle was collected and thrown away.
 *
 * Round 3 closes all three, and adds a SECOND expression whose answer cannot be confused with
 * noise: 1+1 gives "2", one glyph, and a single stray byte can imitate it. 123*456 gives "56088".
 *
 * SAFETY: rounds 1 and 2 made these same nine argument shapes 99 times across 11 launches with ZERO
 * resets, so the call itself is now known-safe on this OS. The new risk is reading 32 bytes behind
 * two OS-returned pointers. Both were allocated by the OS in this process and both are non-NULL
 * before we touch them; the counter is still persisted before every attempt, so a reset still costs
 * one relaunch and not the sweep.
 */

/* Raw bytes, before any decoding. GAP 1: an empty string and a mis-read string print identically
 * once decoded, and only one of those two is a result. */
static void dump_bytes(const char *name, const void *p, int n) {
    const unsigned char *b = (const unsigned char *)p;
    char line[160]; int o = 0;
    o += snprintf(line + o, sizeof line - o, "    %s @%p:", name, p);
    for (int i = 0; i < n && o < (int)sizeof line - 4; i++)
        o += snprintf(line + o, sizeof line - o, " %02X", b[i]);
    say("%s", line);
}

/* Decode a UTF-16 out-pointer THREE ways, because the encoding is not established.
 * Returns 1 if any of them produced a non-empty string. */
static int show_string(uint16_t *out) {
    dump_bytes("raw out[0..31]", out, 32);

    unsigned n = (unsigned)utf16_strlen(out);
    say("    utf16_strlen=%u", n);

    int got = 0;
    if (n) {                                   /* route A: the SDK's own decoder, as round 2 did */
        char a[128]; unsigned m = n > 120 ? 120 : n;
        utf162ascii(a, out, (int)m); a[m] = 0;
        say("    utf162ascii -> \"%s\"", a);
        if (a[0]) got = 1;
    }
    {   /* route B: read as UTF-16 LITTLE endian by hand, ignoring utf16_strlen entirely. If the
         * length walk is what failed, this still recovers the text. */
        char a[64]; int k = 0;
        for (int i = 0; i < 60 && out[i]; i++) {
            uint16_t c = out[i];
            a[k++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
        }
        a[k] = 0;
        say("    manual LE     -> \"%s\" (%d units)", a, k);
        if (k) got = 1;
    }
    {   /* route C: BIG endian. A BE '2' is 00 32, whose low byte is zero -- which is exactly what
         * an LE-assuming length walk reads as a terminator on the very first unit. This is the
         * single most likely explanation for a non-NULL pointer decoding to "". */
        const unsigned char *b = (const unsigned char *)out;
        char a[64]; int k = 0;
        for (int i = 0; i < 60; i++) {
            uint16_t c = (uint16_t)((b[2*i] << 8) | b[2*i + 1]);
            if (!c) break;
            a[k++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
        }
        a[k] = 0;
        say("    manual BE     -> \"%s\" (%d units)", a, k);
        if (k) got = 1;
    }
    return got;
}

/* Given a successful evaluate, try every plausible way of handing the result to MathExprToStr.
 * Each candidate is built only from memory we own or values the OS just wrote there.
 *
 * `hblk` is whichever buffer the OS actually wrote the handle into -- b4 for the A_BLOCK attempts,
 * &i4 for the A_INT one. GAP 3: round 2 always passed b4, so attempt 8's handle was collected in i4
 * and then never used. Returns 1 if a NON-EMPTY string was produced. */
static int try_render(char *hblk, const char *which) {
    uint32_t *w = (uint32_t *)(void *)hblk;
    say("  handle source: %s, first word = %08lX", which, (unsigned long)w[0]);
    if (w[0]) dump_bytes("result object *[0..31]", (const void *)(uintptr_t)w[0], 32);

    struct { const char *desc; void *h1; void *h2; } C[] = {
        { "h1=blk         h2=NULL",        (void *)hblk,        0 },
        { "h1=*(void**)blk h2=NULL",       (void *)(uintptr_t)w[0], 0 },
        { "h1=NULL        h2=blk",         0,                   (void *)hblk },
        { "h1=blk         h2=blk+4",       (void *)hblk,        (void *)(hblk + 4) },
        { "h1=*(void**)blk h2=*(void**)blk+4", (void *)(uintptr_t)w[0], (void *)(uintptr_t)w[1] },
        { "h1=&blk        h2=NULL",        (void *)&hblk,       0 },
    };
    int any = 0;
    for (unsigned i = 0; i < sizeof C / sizeof C[0]; i++) {
        uint16_t *out = 0;
        say("  render try %u: %s", i + 1, C[i].desc);
        int rc = TI_MS_MathExprToStr(C[i].h1, C[i].h2, &out);
        say("    rc=%d out=%p", rc, (void *)out);
        if (rc == 0 && out) {
            /* DO NOT return here. GAP 2: round 2 stopped on the first non-NULL pointer, which was
             * arrangement 2 with an empty string, and arrangements 3-6 were never tried. */
            if (show_string(out)) {
                say("    *** NON-EMPTY STRING FROM ARRANGEMENT %u. The OS CAS IS DRIVEABLE. ***", i + 1);
                any = 1;
            } else {
                say("    (rc=0, pointer valid, no text recovered by any of the three decoders)");
            }
        }
    }
    if (!any) say("  no handle arrangement produced a non-empty string");
    return any;
}

/* ---- CAS ITEM 1: HOW LONG DOES A CALL TAKE? ---------------------------------------------------
 *
 * The one number that opens or closes "delegate calculus to the OS CAS", and it has never been
 * taken. A tool call sits inside a token budget of ~475 ms at the chosen d352 L6 (2.11 tok/s). If a
 * CAS call costs 400 us it is free; if it costs 4 s the idea is dead, and "driveable but too slow"
 * is still a result worth the poster.
 *
 * IT IS CALLED FROM BOTH EXITS OF main(). The attempt counter on the device already reads 9 of 9,
 * so the NEXT launch takes the early return -- and a timing phase placed only after the attempt
 * loop would never have executed. A block of code with no reachable caller reads as a feature and
 * is not one; that is a recorded failure in this repo (IN_SCROLL handled and never emitted).
 *
 * Timed on the 99 MHz FAST timer, not the 32 kHz one: at 32 kHz a sub-millisecond call reads as
 * 0 ticks, and a stage that reads zero is indistinguishable from a stage that is free. That exact
 * confusion cost this project a session on the FFN timer. */
static void cas_timing(void)
{
    static uint16_t texpr[32];
    static char q1[512], q2[512], q4[512], q5[512];
    bench_timer_t t;
    unsigned long total = 0, lo = 0xFFFFFFFFul, hi = 0, ok = 0, n = 0;
    const int N_CALLS = 100;

    /* ITS OWN FILE, NOT THE 114 KB APPEND-ONLY LOG. The timing block was written into
     * caslog.txt.tns, and that file then refused to transfer: `nsp pull` returned "Invalid packet
     * received" on six consecutive attempts while a LARGER file (the 145 KB tokenizer) pulled
     * cleanly, so the fault is the file and not the link. The number was measured, is on the
     * device, and cannot be read -- which is the same as not having measured it.
     *
     * A result that has to survive a transfer belongs in a small file of its own. */
    {
        char tp[80];
        for (int i = 0; DIRS[i]; i++) {
            snprintf(tp, sizeof tp, "%scastime.txt.tns", DIRS[i]);
            FILE *tf = fopen(tp, "w");
            if (tf) { fprintf(tf, "castime placeholder\n"); fclose(tf); TIMEPATH_OK = 1; break; }
        }
    }
    say("");
    say("=== CAS timing: %d calls, arrangement 'all blocks', 99 MHz timer ===", N_CALLS);
    if (LOG) fflush(LOG);                      /* flush BEFORE, per this file's own rule */
    timer_acquire(&t, TIMER_FAST_BASE);
    for (int c = 0; c < N_CALLS; c++) {
        memset(texpr, 0, sizeof texpr);
        ascii2utf16(texpr, (char *)"123*456", 7);
        texpr[7] = 0;
        memset(q1, 0, sizeof q1); memset(q2, 0, sizeof q2);
        memset(q4, 0, sizeof q4); memset(q5, 0, sizeof q5);
        uint32_t t0 = timer_raw(TIMER_FAST_BASE);
        int rc = TI_MS_evaluateExpr_ACBER(q1, q2, texpr, q4, q5);
        uint32_t t1 = timer_raw(TIMER_FAST_BASE);
        uint32_t d  = timer_delta(t0, t1);
        if (rc == 0) ok++;
        total += d; n++;
        if (d < lo) lo = d;
        if (d > hi) hi = d;
    }
    timer_release(&t);
    /* 64-bit before scaling: the us conversion in bench_forward overflowed 32-bit long and printed
     * NEGATIVE MICROSECONDS, which read as a finding about the machine rather than about itself. */
    unsigned long long mean_us = n ? (unsigned long long)total * 1000000ull / 99000000ull / n : 0;
    say("CAS_calls=%lu", n);
    say("CAS_rc0=%lu of %lu", ok, n);
    say("CAS_mean_us=%llu", mean_us);
    say("CAS_min_us=%llu", (unsigned long long)lo * 1000000ull / 99000000ull);
    say("CAS_max_us=%llu", (unsigned long long)hi * 1000000ull / 99000000ull);
    say("CAS_note=evaluate only, no render. A usable tool call also needs the UTF-16 decode.");
    say("CAS_budget=compare against ~470000 us/token at 2 tok/s.");
    /* Write the numbers to the small file as well, so one bad log cannot lose them again. */
    if (TIMEPATH_OK) {
        for (int i = 0; DIRS[i]; i++) {
            char tp2[80]; snprintf(tp2, sizeof tp2, "%scastime.txt.tns", DIRS[i]);
            FILE *tf = fopen(tp2, "w");
            if (!tf) continue;
            fprintf(tf, "CAS_calls=%lu\nCAS_rc0=%lu\nCAS_mean_us=%llu\nCAS_min_us=%llu\n"
                        "CAS_max_us=%llu\n", n, ok, mean_us,
                    (unsigned long long)lo * 1000000ull / 99000000ull,
                    (unsigned long long)hi * 1000000ull / 99000000ull);
            fclose(tf); break;
        }
    }
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

    say("=== bench_cas ROUND 3 ===");   /* rounds are appended to one log; label them */
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
        cas_timing();                          /* the path the device will actually take: 9 of 9 done */
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

    /* TWO expressions per attempt. "1+1" -> "2" is one glyph, and one stray byte can imitate it;
     * "123*456" -> "56088" cannot be produced by accident. If only the short one ever renders, the
     * result is noise. */
    static const struct { const char *txt; const char *want; } EXPRS[] = {
        { "1+1",     "2"     },
        { "123*456", "56088" },
    };
  for (unsigned ei = 0; ei < sizeof EXPRS / sizeof EXPRS[0]; ei++) {
    /* CLEAR THE BUFFER. It is `static`, ascii2utf16 does not terminate, and the expressions are
     * different lengths -- so writing "1+1" (3 units) over "123*456" (7 units) left the tail and
     * sent `1+1*456`. utf16_strlen then reported 7 for a 3-character expression, which is the tell
     * that was printed on every run and not read.
     *
     * THE ACCIDENT WAS A BETTER EXPERIMENT THAN THE TEST. The CAS returned 457, and
     * 1 + 1*456 = 457 with correct operator precedence -- on an expression nothing had ever sent.
     * A stale buffer, a cached constant or a garbage heap read cannot produce that; only a real
     * parser can. It is stronger evidence that the OS CAS is driveable than the 123*456 -> 56088
     * case it was meant to support, because that one could in principle have been a coincidence of
     * adjacent memory. Keeping the note because the corrected harness can no longer reproduce it. */
    static uint16_t expr[32];
    memset(expr, 0, sizeof expr);
    ascii2utf16(expr, (char *)EXPRS[ei].txt, (int)strlen(EXPRS[ei].txt));
    expr[strlen(EXPRS[ei].txt)] = 0;                 /* belt and braces: terminate explicitly */
    say("expr=\"%s\" (expect \"%s\") marshalled: utf16_strlen=%u",
        EXPRS[ei].txt, EXPRS[ei].want, (unsigned)utf16_strlen(expr));
    dump_bytes("expr utf16[0..15]", expr, 16);

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
        /* GAP 3: the handle goes wherever p4 pointed. For A_INT that is &i4, not b4 -- and round 2
         * passed b4 unconditionally, so attempt 8's handle (i4 = 0x116355D0) was printed and then
         * discarded. Pass the buffer the OS was actually given. */
        if (a->p4_kind == A_INT) try_render((char *)&i4, "&i4 (p4 was an int slot)");
        else                     try_render(b4,          "b4 (p4 was a block)");
    } else {
        say("rc=%d (1020 = invalid argument on this API), not rendering", rc);
    }
  }   /* end expression loop */

    say("attempt %d survived.", idx + 1);
  }
    say("");
    say("ALL %d ATTEMPTS COMPLETE in this launch -- none of the remaining ones reset.", NATTEMPTS);

    cas_timing();

    if (LOG) fclose(LOG);
    printf("\nlog: %s\nPress any key.\n", LOG_PATH);
    wait_key_pressed();
    return 0;
}
