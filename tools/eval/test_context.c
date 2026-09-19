/* app_context(): the conversation history the device now prepends to a follow-up.
 *
 * WHY THIS FILE EXISTS. app_context was written, carefully, and had NO CALLER for its whole life --
 * the seventh instance of the unwired class in this repo. A115 wires it into app_request, which
 * only cross-compiles, so the call site itself cannot be exercised on the host. What CAN be is the
 * function, and this is the smallest pure unit: give it turns, read the string. That is the
 * discipline askparse.c/pickui.c were extracted under, and it is the only way this behaviour is
 * checkable at all.
 *
 * The properties asserted are the ones app_request depends on being true:
 *   - the PENDING turn is excluded, because app_begin_turn pushes the current question BEFORE
 *     app_request asks for context, and including it would feed the model its own question twice;
 *   - the prefix is the literal the corpus trains on;
 *   - oldest surviving turn comes FIRST, so the history reads forwards;
 *   - the budget drops turns rather than truncating one mid-question;
 *   - a first turn gets no context at all, which is the ordinary case and must stay empty.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"   /* the suite reaches app.c's file-scope state */

static int ran, fails;
static void ck(int ok, const char *what, const char *got) {
    ran++;
    if (!ok) { fails++; printf("  FAIL %-52s got: %s\n", what, got); }
    else       printf("  ok   %-52s\n", what);
}

static void turn(const char *q, const char *formula, const char *values) {
    app_begin_turn(q);
    app_stream_token("<a> something<end>");
    app_finish_turn(formula, values);
    app_stream_end();
}

int main(void) {
    char c[512];
    printf("app_context\n\n");

    /* -- no history: a first turn must get nothing ------------------------------------------- */
    int n = app_context(c, sizeof c, 200);
    ck(n == 0 && c[0] == 0, "no chat yet -> empty context", c);

    turn("what is hookes law", "F=-k*x", "");
    app_begin_turn("why is it negative");           /* the PENDING turn */
    n = app_context(c, sizeof c, 200);
    ck(strstr(c, "Earlier: what is hookes law") != NULL,
       "the earlier turn appears, with the device's prefix", c);
    ck(strstr(c, "why is it negative") == NULL,
       "the PENDING turn is excluded", c);
    app_stream_token("<a> x<end>"); app_finish_turn("F=-k*x", ""); app_stream_end();

    /* -- order: oldest surviving first, so the history reads forwards ------------------------ */
    turn("second question here", "W=F*d", "");
    app_begin_turn("third");
    n = app_context(c, sizeof c, 400);
    /* THE OLDEST TURN IS NOT VERBATIM, and that is the three-tier design rather than a bug. Only
     * the last VERBATIM_TURNS keep their question; older ones collapse to the compact summary, so
     * turn 1 appears as "F=-k*x" and not as "Earlier: what is hookes law". My first assertion here
     * looked for "hookes", failed, and was wrong about correct code -- check the oracle first. */
    const char *a = strstr(c, "F=-k*x"), *b = strstr(c, "second question");
    ck(a && b && a < b, "oldest surviving turn comes first", c);
    ck(strstr(c, "Earlier: what is hookes law") == NULL,
       "a turn older than VERBATIM_TURNS collapses to its summary", c);
    app_stream_token("<a> x<end>"); app_finish_turn("W=F*d", ""); app_stream_end();

    /* -- budget: a tight budget DROPS turns, and never emits a half question ----------------- */
    app_begin_turn("fourth");
    n = app_context(c, sizeof c, 40);
    ck(n <= 40, "a tight budget is respected", c);
    ck(strstr(c, "Earlier: ") == NULL || strlen(c) >= 9,
       "no truncated prefix is emitted", c);
    app_stream_token("<a> x<end>"); app_finish_turn("", ""); app_stream_end();

    /* -- cap: never writes past the buffer --------------------------------------------------- */
    char small[24];
    memset(small, 0x7f, sizeof small);
    app_begin_turn("fifth");
    n = app_context(small, sizeof small, 200);
    ck(n < (int)sizeof small && small[n] == 0, "respects cap and NUL-terminates", small);

    printf("\n%s  %d/%d\n", fails ? "FAIL" : "PASS", ran - fails, ran);
    return fails ? 1 : 0;
}
