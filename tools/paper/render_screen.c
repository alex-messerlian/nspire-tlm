/* render_screen -- one finished exchange, drawn by the shipped app.c exactly as the calculator draws it.
 *
 *   render_screen <out.ppm> <light|dark> <question> <emitted text>    a question and its answer
 *   render_screen <out.ppm> <light|dark>                              the start screen
 *
 * NOT A MOCK, AND NO STEP IS WRITTEN AGAIN HERE. The question goes into the compose buffer and
 * open_picker() -- the path the enter key takes -- chooses the relation, as tools/eval/autoasm.c
 * does. Then the steps of device_app.c's app_request, in its order: app_begin_turn, ask_build, the
 * record named by the chosen rid, the emitted text streamed in, app_status_done, app_finish_turn
 * with the formula and the values the calculator parsed (so summarise() builds the line above the
 * answer, stray period and all), app_stream_end. The emitted text is the calculator decoder's output
 * replayed on the host (build/int8gen), which matches the device token for token.
 *
 * The side panel is closed and the pointer is off the screen, as on a calculator nobody is touching.
 * The frame is the 320x240 LCD, pixel for pixel; tools/paper/make_screens.py enlarges it. */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"

extern char HS_LASTQ[512], HS_LASTR[64];
extern int HS_NREQ;

int main(int argc, char **argv) {
    if (argc != 3 && argc != 5) {
        fprintf(stderr, "usage: render_screen <out.ppm> <light|dark> [<question> <emitted text>]\n");
        return 2;
    }
    gfx_init();
    app_init();
    app_set_theme(!strcmp(argv[2], "dark") ? TH_DARK : TH_LIGHT);
    SIDEBAR = 0;
    if (argc == 5) {
        const ns_store2 *st = app_store();
        if (!st) { fprintf(stderr, "render_screen: build/store.tns did not load\n"); return 2; }
        snprintf(COMPOSE, sizeof COMPOSE, "%s", argv[3]);
        COMPOSE_N = (int)strlen(COMPOSE);
        int sent = HS_NREQ;
        open_picker();
        if (HS_NREQ != sent + 1) { fprintf(stderr, "render_screen: the app did not send the question\n"); return 2; }

        /* device_app.c app_request, step for step (generation replaced by the recorded output) */
        app_begin_turn(HS_LASTQ);
        static ns_ask ask;
        ask_build(st, HS_LASTQ, &ask);
        const ns_input in = ask.in;
        int idx = -1;
        if (HS_LASTR[0])
            for (int r = 0; r < st->n; r++)
                if (st->rec[r].rid && !strcmp(st->rec[r].rid, HS_LASTR)) { idx = r; break; }
        app_stream_token(argv[4]);
        app_status_done(0, 0, 0, 1);
        char vals[64]; vals[0] = 0;
        for (int i = 0; i < in.nvals; i++) {
            char one[24];
            snprintf(one, sizeof one, "%s=%s ", in.var[i], in.val[i]);
            if (strlen(vals) + strlen(one) < sizeof vals) strcat(vals, one);
        }
        app_finish_turn(idx < 0 ? "none" : st->rec[idx].formula, vals);
        app_stream_end();
        printf("rid %s  record %s\n", idx < 0 ? "-" : HS_LASTR, idx < 0 ? "(none)" : st->rec[idx].formula);
    }
    MX = MY = 1000;                     /* the pointer, off the screen */
    app_draw();

    FILE *f = fopen(argv[1], "wb");
    if (!f) { perror(argv[1]); return 1; }
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
    gfx_free();
    return 0;
}
