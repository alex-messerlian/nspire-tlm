/* The prompt the APP sends for a typed question, with the app choosing the relation itself.
 *
 * Reads one question per line on stdin; prints "rid<TAB>prompt" per line, rid "-" when the app
 * found no confident relation and sent Form C (the prompt with no record), or "ERR <reason>".
 *
 * WHY THIS EXISTS. Every quality number so far supplied the RIGHT relation (devasm takes a rid).
 * Since A125 the app picks the relation itself and declines when it is not confident, so a
 * student's answer depends on that choice first. Scoring the shipped app end to end needs the
 * choice the app actually makes.
 *
 * THE SELECTION IS NOT RE-IMPLEMENTED HERE. This file #includes app.c, fills the compose buffer
 * and calls open_picker(), the function the enter key reaches on the calculator (test_autopick
 * drives it the same way). The host stub for app_request records the question and rid it was
 * sent. Only the prompt assembly after that is replayed, in app_request's order -- ask_build, then
 * ns_assemble on the chosen record or ns_assemble_none -- which is devasm's sequence and is checked
 * against the calculator's own logged prompts by parity_toolloop.py.
 *
 * The compose buffer is the device's (160 bytes), so a longer question is truncated exactly as the
 * keypad would truncate it. A single question has no earlier turns, so app_context adds nothing.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"

extern char HS_LASTQ[512], HS_LASTR[64];
extern int HS_NREQ;

int main(void) {
    gfx_init();
    app_init();
    const ns_store2 *st = app_store();
    if (!st) { puts("ERR nostore"); return 2; }
    static char line[4096], out[NS_PROMPT_MAX];
    static ns_ask ask;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        int sent = HS_NREQ;
        snprintf(COMPOSE, sizeof COMPOSE, "%s", line);
        COMPOSE_N = (int)strlen(COMPOSE);
        open_picker();
        if (HS_NREQ != sent + 1) { puts("ERR nosend"); continue; }

        /* app_request(HS_LASTQ, rid), device_app.c: a rid that names no record is Form C too. */
        ask_build(st, HS_LASTQ, &ask);
        int idx = -1;
        if (HS_LASTR[0])
            for (int r = 0; r < st->n; r++)
                if (st->rec[r].rid && !strcmp(st->rec[r].rid, HS_LASTR)) { idx = r; break; }
        int rc = idx < 0 ? ns_assemble_none(out, sizeof out, ask.question)
                         : ns_assemble(out, sizeof out, &st->rec[idx], ask.question, &ask.in);
        if (rc < 0) { puts("ERR overflow"); continue; }
        printf("%s\t%s\n", idx < 0 ? "-" : HS_LASTR, out);
    }
    return 0;
}
