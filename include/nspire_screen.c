/* nspire_screen.c -- the single definition of the console state.
 *
 * Deliberately not in the header: `static` there gave each translation unit its own copy, so a
 * console initialised in one file was invisible to every other one. */
#include "nspire_screen.h"

#ifndef NSPIRE_HOST_TEST

nio_console g_csl;
int g_screen_ready = 0;

void screen_init(void) {
    if (g_screen_ready) return;
    /* Full-width, full-height console: 53x30 on the Nspire. White on black. */
    if (nio_init(&g_csl, NIO_MAX_COLS, NIO_MAX_ROWS, 0, 0,
                 NIO_COLOR_BLACK, NIO_COLOR_WHITE, true)) {
        nio_set_default(&g_csl);
        g_screen_ready = 1;
    }
}

void screen_free(void) {
    if (g_screen_ready) { nio_free(&g_csl); g_screen_ready = 0; }
}
#endif
