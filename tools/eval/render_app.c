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
    /* A46. AN EXPLANATION, which is the longest answer shape the device can produce and the only
     * one with NO tool call. Seeded here because "render the screen before pushing it" is a rule
     * this repo paid a device round-trip for -- draw_picker once drew fifteen rows straight through
     * its own key legend, and no assertion could see it. An explanation is longer than a computed
     * answer (a formula plus a units clause for every variable), so it is the case most likely to
     * overflow or clip. Real text, taken verbatim from corpus/synth_sample.jsonl. */
    seed("Explain Newton's second law",
         "explain Newton's second law in a sentence.",
         "<a>Newton's second law is F_net = m*a, with F_net in N, m in kg and a in m/s^2.<end>");
    seed("What is the drag force",
         "how is drag force related to the other quantities?",
         "<a>F_D = ((1)/(2))*C*rho*A*(v)^(2) gives drag force, where F_D in N, C is dimensionless, "
         "rho in kg/m^3, A in m^2 and v in m/s.<end>");

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
    /* THE PICKER, at each of its three states. Rendering it here is how the layout was checked
     * without a device round-trip -- the same draw_picker the calculator runs. */
    if (!strncmp(screen, "pick", 4)) {
        snprintf(COMPOSE, sizeof COMPOSE, "A 3.0 kg block accelerates at 4.5 m/s^2. What net force?");
        COMPOSE_N = (int)strlen(COMPOSE);
        CUR = -1; MX = 300; MY = 220;
        open_picker();
        if (!strcmp(screen, "pickrec"))   { PK.sel = PK.nsug + 4; app_event(&(in_event){.kind=IN_KEY,.key=K_ENTER}); }
        /* NOT "capacit" -- PICKER_SPEC uses it as the no-match example and this store has five
         * capacitance records, so it renders a full list. The empty screen needs a real miss. */
        if (!strcmp(screen, "pickempty")) { const char *q = "zzqq";
            for (const char *c = q; *c; c++) app_event(&(in_event){.kind=IN_KEY,.key=*c}); }
        if (!strcmp(screen, "pickfind"))  { const char *q = "force";
            for (const char *c = q; *c; c++) app_event(&(in_event){.kind=IN_KEY,.key=*c}); }
        app_draw(); goto out;
    }
    if (!strcmp(screen, "palette")) {
        CUR = -1; MX = 300; MY = 220; FIELD_FOCUS = 1;
        const char *q = "Find kinetic energy. Given m = 2";
        for (const char *c = q; *c; c++) app_event(&(in_event){.kind=IN_KEY,.key=*c});
        app_event(&(in_event){.kind=IN_KEY,.key=K_SYM});
        for (int i = 0; i < 5; i++) app_event(&(in_event){.kind=IN_KEY,.key=K_RIGHT});
        app_draw(); goto out;
    }
    if (!strncmp(screen, "caret", 5)) {
        CUR = -1; MX = 300; MY = 220; FIELD_FOCUS = 1;
        const char *q = "Find velocity. Given v_0 = 5, a = 2";
        for (const char *c = q; *c; c++) app_event(&(in_event){.kind=IN_KEY,.key=*c});
        int back = atoi(screen + 5);              /* caretN -> N presses of LEFT */
        for (int i = 0; i < back; i++) app_event(&(in_event){.kind=IN_KEY,.key=K_LEFT});
        NOW_MS = 0;                                /* caret ON: (NOW_MS/500)%2 == 0 */
        app_draw(); goto out;
    }
    if (!strcmp(screen, "correction")) {
        /* THE EXACT DEVICE TRANSCRIPT. It rendered and the reader did not act on it, which is the
         * defect being fixed -- so the fix is checked by LOOKING, not by asserting it is present. */
        seed("Kinetic energy", "Find kinetic energy. Given m = 900, v = 800",
             "<tool> eval<arg> 0.5*(900.0)*((800.0))^(2)</tool><res> 288000000</res>"
             "<a> K = 229 J. From K=0.5*m*(v)^(2).<end>"
             "The sentence above misstates the number. The calculator computed 288000000 J.");
        CUR = NCHATS - 1; MX = 300; MY = 220;
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
    } else if (!strcmp(screen, "explain")) {
        CUR = 4; MX = 300; MY = 220;          /* A46: the explanation turn */
    } else if (!strcmp(screen, "explain2")) {
        CUR = 5; MX = 300; MY = 220;          /* A46: the longest explanation the store can produce */
    } else if (!strcmp(screen, "actions")) {
        /* Pointer parked on the first answer, which is what reveals the action row. The transcript
         * has to be drawn once first: R_ANS is filled during the draw, and hovering is meaningless
         * before anything knows where the answer is. */
        CUR = 2; HOVER = 1; MX = 300; MY = 220;
        app_draw();
        MX = R_ANS[0].x + 20; MY = R_ANS[0].y + 6;
    } else if (!strcmp(screen, "draftsel")) {
        /* ctrl+a with a draft in the box: the field's own select-all. */
        CUR = 2; HOVER = 1; MX = 300; MY = 220; FIELD_FOCUS = 1;
        snprintf(COMPOSE, sizeof COMPOSE, "%s",
                 "A 4 kg cart moves at 3 m/s. Find the kinetic energy.");
        COMPOSE_N = (int)strlen(COMPOSE);
        COMPOSE_SEL = 1;
    } else if (!strcmp(screen, "qcopy")) {
        /* Pointer on the QUESTION bubble, which is what reveals its copy control. */
        CUR = 2; HOVER = 1; MX = 300; MY = 220;
        app_draw();
        MX = R_QACT[0].x - 30; MY = R_QACT[0].y - 8;
    } else if (!strcmp(screen, "selall")) {
        CUR = 2; HOVER = 1; MX = 300; MY = 220;
        SEL_ALL = 1;
    } else if (!strcmp(screen, "selected")) {
        /* A live selection, made the way a drag makes one: probe the text, then extend. */
        CUR = 2; HOVER = 1; MX = 300; MY = 220;
        app_draw();
        sel_begin(R_ANS[0].x + 4, R_ANS[0].y + 4);
        sel_extend(R_ANS[0].x + 120, R_ANS[0].y + 4);
    } else if (!strcmp(screen, "typing") || !strcmp(screen, "typinglong")) {
        CUR = 2; MX = 300; MY = 220;
        const char *s = !strcmp(screen, "typinglong")
            ? "What is the distance from What is the distance from What is the distance from "
              "What is the distance from What is the distance from"
            : "What is the distance from What is the distance from What is";
        snprintf(COMPOSE, sizeof COMPOSE, "%s", s);
        COMPOSE_N = (int)strlen(COMPOSE);
        app_set_theme(TH_DARK);
    } else if (!strcmp(screen, "settings") || !strcmp(screen, "settingsdark")) {
        CUR = -1; MX = 300; MY = 220; SETTINGS_ON = 1;
        if (!strcmp(screen, "settingsdark")) app_set_theme(TH_DARK);
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
