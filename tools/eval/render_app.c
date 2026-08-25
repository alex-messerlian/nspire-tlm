/* Render a REAL app.c frame at 320x240 and write it out. Not a mock: this drives the same
 * draw_sidebar / draw_main / draw_search the calculator runs, so a layout bug shows up here in two
 * seconds instead of a device round-trip.
 *
 * usage: render_app <out.ppm> [screen] [query]      screen: home | chat | search
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/store/app.c"

static void seed(const char *title, const char *q, const char *a) {
    app_chat *c = &CHATS[NCHATS++];
    memset(c, 0, sizeof *c);
    snprintf(c->title, sizeof c->title, "%s", title);
    snprintf(c->turn[0].q, sizeof c->turn[0].q, "%s", q);
    snprintf(c->turn[0].a, sizeof c->turn[0].a, "%s", a);
    c->nturns = 1; c->used = 1; c->turn[0].done = 1;
}
int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "/tmp/app.ppm";
    const char *screen = argc > 2 ? argv[2] : "search";
    const char *query = argc > 3 ? argv[3] : "energy";

    gfx_init();
    app_init();
    seed("A car goes 150 m in 12 s", "A car goes 150 m in 12 s. Find the speed.",
         "<a>The speed is <r>12.5</r> m/s, straight from v = d/t.<end>");
    seed("3 A through a 4 ohm resistor", "3 A flows through 4 ohm. Find the voltage.",
         "<a>The voltage is <r>12</r> V by Ohm law.<end>");
    seed("A 2 kg mass raised 5 m", "A 2 kg mass is raised 5 m. Find the potential energy.",
         "<a>The gravitational potential energy is <r>98</r> J from U = mgh.<end>");
    seed("Cart on a level track", "A 4 kg cart moves at 3 m/s. Find the kinetic energy.",
         "<a>The kinetic energy is <r>18</r> J from K = mv^2/2.<end>");

    if (!strcmp(screen, "search")) {
        SEARCH_ON = 1; snprintf(SQ, sizeof SQ, "%s", query); SQ_N = (int)strlen(SQ);
        run_search();
        MX = 300; MY = 220;                 /* cursor parked off the sheet */
    } else if (!strcmp(screen, "chat")) {
        CUR = 0; MX = 300; MY = 220;
    } else {
        MX = 46; MY = 50; HOVER = 1;        /* hovering the Search row, to show its state */
    }
    app_draw();

    FILE *f = fopen(out, "wb");
    if (!f) { perror("ppm"); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H);
    const uint16_t *b = gfx_buf();
    for (int i = 0; i < GFX_W * GFX_H; i++) {
        uint16_t c = b[i];
        unsigned char rgb[3] = { (unsigned char)(((c >> 11) & 31) * 255 / 31),
                                 (unsigned char)(((c >> 5) & 63) * 255 / 63),
                                 (unsigned char)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    printf("  wrote %s (%s)\n", out, screen);
    gfx_free();
    return 0;
}
