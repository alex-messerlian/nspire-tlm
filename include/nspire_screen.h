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

#include <nspireio/nspireio.h>
#include <stdarg.h>
#include <stdio.h>

static nio_console g_csl;
static int g_screen_ready = 0;

static inline void screen_init(void) {
    if (g_screen_ready) return;
    /* Full-width, full-height console: 53x30 on the Nspire. White on black. */
    if (nio_init(&g_csl, NIO_MAX_COLS, NIO_MAX_ROWS, 0, 0,
                 NIO_COLOR_BLACK, NIO_COLOR_WHITE, true)) {
        nio_set_default(&g_csl);
        g_screen_ready = 1;
    }
}

static inline void screen_free(void) {
    if (g_screen_ready) { nio_free(&g_csl); g_screen_ready = 0; }
}

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
    nio_puts(buf);
    return n;
}

static inline void screen_flush(void) {
    if (g_screen_ready) nio_fflush(nio_get_default());
}

#define printf screen_printf

#endif
