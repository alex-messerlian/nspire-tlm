/* nspire_screen.h -- make printf actually appear on the calculator.
 *
 * MEASURED THE HARD WAY: plain printf under Ndless produces a BLACK SCREEN. Ndless does not wire
 * stdout to the display; nspireio does, and it has to be initialised explicitly. Every program in
 * this project ran blind until this existed, which made a slow load indistinguishable from a hang.
 *
 * The logs remain the source of truth -- they are fflushed per line and survive a reset, which the
 * screen does not. This is for the operator's benefit and, eventually, the demo.
 *
 * Usage: call screen_init() first thing in main(), then use printf normally.
 */
#ifndef NSPIRE_SCREEN_H
#define NSPIRE_SCREEN_H

#include <stdarg.h>
#include <stdio.h>

/* Host builds (NSPIRE_HOST_TEST) have no nspireio and no framebuffer -- keep plain printf so the
 * same sources can be validated against the golden output on the host. */
#ifdef NSPIRE_HOST_TEST
static inline void screen_init(void)  {}
static inline void screen_free(void)  {}
static inline void screen_flush(void) { fflush(stdout); }
#else
#include <nspireio/nspireio.h>

/* EXTERN, not static.
 *
 * These were `static` in this header, which gives every translation unit its OWN copy. screen_init()
 * runs in nspire_main.c and set THAT TU's flag; runq_nspire.c and nspire.c each had a separate copy
 * still at zero, so their screen_printf calls returned early and printed nothing. Every trace and
 * every generated token from the engine was silently discarded, which looked exactly like a freeze.
 * One definition, in nspire_screen.c. */
extern nio_console g_csl;
extern int g_screen_ready;

void screen_init(void);
void screen_free(void);

/* Route printf to the console, but GUARDED. If nio_init failed we must not call into nspireio with
 * no default console -- a crash is strictly worse than the black screen we are trying to fix. A
 * failed init degrades to silent, which is the pre-existing behaviour. */
static inline int screen_printf(const char *fmt, ...) {
    if (!g_screen_ready) return 0;
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    /* nio_fputs, NOT nio_puts: nio_puts follows C's puts() and APPENDS A NEWLINE. generate() emits
     * one token per printf, so nio_puts put every token -- and every BPE sub-word fragment -- on its
     * own line. The text was correct; the layout was not. */
    nio_fputs(buf, nio_get_default());
    return n;
}

static inline void screen_flush(void) {
    if (g_screen_ready) nio_fflush(nio_get_default());
}

/* stderr and stdout both BLOCK under Ndless -- there is no console behind them. runq.c reports every
 * error via fprintf(stderr, ...), so an error path would hang instead of reporting. Route those to
 * the console; leave real file streams alone so log writes still work. */
static inline int screen_fprintf(FILE *f, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n;
    if (f == stderr || f == stdout) {
        char b[512];
        n = vsnprintf(b, sizeof b, fmt, ap);
        if (g_screen_ready) nio_fputs(b, nio_get_default());
    } else {
        n = vfprintf(f, fmt, ap);
    }
    va_end(ap);
    return n;
}

/* Matches C's puts(): appends a newline, unlike screen_printf. */
static inline int screen_puts(const char *s) {
    if (!g_screen_ready) return 0;
    nio_fputs(s, nio_get_default());
    nio_fputs("\n", nio_get_default());
    return 0;
}

static inline int screen_putchar(int c) {
    if (!g_screen_ready) return c;
    char b[2] = { (char)c, 0 };
    nio_fputs(b, nio_get_default());
    return c;
}

#define printf   screen_printf
#define fprintf  screen_fprintf
#define puts     screen_puts
#define putchar  screen_putchar
#endif /* NSPIRE_HOST_TEST */

#endif
