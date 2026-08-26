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
#include "../../src/store/toolrun.h"

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
         "<tool>eval 150/12</tool><res>12.5</res><a>The speed is 12.5 m/s, straight from v = d/t.<end>");
    seed("3 A through a 4 ohm resistor", "3 A flows through 4 ohm. Find the voltage.",
         "<tool>eval 3*4</tool><res>12</res><a>The voltage is 12 V by Ohm law.<end>");
    seed("A 2 kg mass raised 5 m", "A 2 kg mass is raised 5 m. Find the potential energy.",
         "<tool>eval 2*9.8*5</tool><res>98</res><a>The gravitational potential energy is 98 J from U = mgh.<end>");
    seed("Cart on a level track", "A 4 kg cart moves at 3 m/s. Find the kinetic energy.",
         "<tool>eval 4*9/2</tool><res>18</res><a>The kinetic energy is 18 J from K = mv^2/2.<end>");

    if (!strcmp(screen, "full")) {          /* twelve sessions: exercises the scrollbar */
        const char *n[] = { "Terminal velocity of a sphere", "Charge on a capacitor",
                            "Doppler shift of a siren", "Half life of carbon 14",
                            "Escape velocity from Mars", "Refraction through a prism",
                            "Torque on a bolt", "Ideal gas at 300 K" };
        for (unsigned i = 0; i < sizeof n / sizeof n[0]; i++)
            seed(n[i], "worked question", "<a>worked answer<end>");
        CUR = 1; CHAT_SCROLL = 3; MX = 300; MY = 220;
        app_draw(); goto out;
    }
    if (!strcmp(screen, "notation")) {     /* the corpus's ASCII, as a reader sees it */
        seed("Notation", "Show me the symbols.",
             "<a>Delta_p = m*Delta_v, and omega = sqrt((k)/(m)). "
             "With lambda_0, theta_1, E=m*c^2, rho, mu and 2*pi*f.<end>");
        CUR = NCHATS - 1; MX = 300; MY = 220;
        app_draw(); goto out;
    }
    if (!strcmp(screen, "dark") || !strcmp(screen, "darksearch")) {
        app_set_theme(TH_DARK);
        CUR = 0;
        CHATS[0].turn[0].done = 1;
        snprintf(CHATS[0].turn[0].sum, sizeof CHATS[0].turn[0].sum, "22.4s  eval(150/12) -> 12.5");
        if (!strcmp(screen, "darksearch")) {
            SEARCH_ON = 1; snprintf(SQ, sizeof SQ, "energy"); SQ_N = 6; run_search();
        }
        MX = 300; MY = 220; app_draw(); goto out;
    }
    if (!strcmp(screen, "status")) {      /* mid-turn: the live line */
        CUR = 0; BUSY = 1;
        snprintf(CHATS[0].turn[0].a, sizeof CHATS[0].turn[0].a, "<a>The speed is 12.5 m/s,");
        CHATS[0].turn[0].done = 0;
        { char lbl[64]; tlm_call_label("<tool>eval<arg>150/12</tool>", lbl, sizeof lbl); app_status("Running", lbl); }
        MX = 300; MY = 220; app_draw(); goto out;
    }
    if (!strcmp(screen, "statusdone")) {  /* after: the line that stays */
        CUR = 0; BUSY = 0;
        CHATS[0].turn[0].done = 1;
        snprintf(CHATS[0].turn[0].sum, sizeof CHATS[0].turn[0].sum,
                 "22.4s  eval(150/12) -> 12.5");
        MX = 300; MY = 220; app_draw(); goto out;
    }
    if (!strcmp(screen, "busy")) {          /* mid-generation: the Stop control and its hint */
        CUR = 0; BUSY = 1;
        snprintf(CHATS[0].turn[0].a, sizeof CHATS[0].turn[0].a,
                 "<a>The speed is 12.5 m/s, straight from");
        MX = 300; MY = 220;
        app_draw(); goto out;
    }
    if (!strcmp(screen, "manyhits")) {      /* more matches than the sheet holds */
        const char *n[] = { "Kinetic energy of a cart", "Potential energy on a ramp",
                            "Energy stored in a spring", "Thermal energy of a gas",
                            "Energy of a photon", "Rotational energy of a disc" };
        for (unsigned i = 0; i < sizeof n / sizeof n[0]; i++)
            seed(n[i], "find the energy", "<a>the energy is 12 J<end>");
        SEARCH_ON = 1; snprintf(SQ, sizeof SQ, "energy"); SQ_N = 6;
        run_search(); SSEL = 5;             /* selection past the visible window */
        MX = 300; MY = 220;
        app_draw(); goto out;
    }
    if (!strcmp(screen, "search")) {
        SEARCH_ON = 1; snprintf(SQ, sizeof SQ, "%s", query); SQ_N = (int)strlen(SQ);
        run_search();
        MX = 300; MY = 220;                 /* cursor parked off the sheet */
    } else if (!strcmp(screen, "chat")) {
        CUR = 2; MX = 300; MY = 220;
    } else if (!strcmp(screen, "typing") || !strcmp(screen, "typinglong")) {
        CUR = 2; MX = 300; MY = 220;
        const char *s = !strcmp(screen, "typinglong")
            ? "What is the distance from What is the distance from What is the distance from "
              "What is the distance from What is the distance from"
            : "What is the distance from What is the distance from What is";
        snprintf(COMPOSE, sizeof COMPOSE, "%s", s);
        COMPOSE_N = (int)strlen(COMPOSE);
        app_set_theme(TH_DARK);
    } else if (!strcmp(screen, "empty")) {
        NCHATS = 0; CUR = -1; MX = 300; MY = 220;   /* the first-run screen, nothing seeded */
    } else if (!strcmp(screen, "emptydark")) {
        NCHATS = 0; CUR = -1; MX = 300; MY = 220;
        app_set_theme(TH_DARK);
    } else {
        MX = 46; MY = 50; HOVER = 1;        /* hovering the Search row, to show its state */
    }
    app_draw();
out:;
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
