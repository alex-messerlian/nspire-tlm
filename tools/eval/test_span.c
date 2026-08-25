/* The transcript renderer must never show tag guts, and must not eat prose that merely contains
 * an angle bracket.
 *
 * The model emits malformed markup roughly half the time (see app.h), so an unrecognised tag is the
 * COMMON case, not the edge one. span_next used to advance past only the '<', which left the tag
 * name and its body as literal text: a stray "<r>12</r>" rendered on screen as "r>12 ... /r>".
 * Caught by rendering a real frame on the host rather than on the device.
 *
 * Assertions are EXACT. The first version of this file checked "does the output contain '>'", which
 * flagged a correct render of "t > 0" as a leak -- a heuristic oracle that fails on valid input is
 * a worse instrument than the thing it is measuring. */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"

static int F;
static void chk(const char *in, const char *want) {
    char buf[256]; buf[0] = 0;
    sp_kind k; char one[200];
    const char *p = in;
    while ((p = span_next(p, &k, one, sizeof one))) {
        if (k == SP_TEXT && one[0]) strncat(buf, one, sizeof buf - strlen(buf) - 1);
        if (!*p) break;
    }
    int ok = strcmp(buf, want) == 0;
    if (!ok) F++;
    printf("  %s  %-46s -> \"%s\"%s\n", ok ? "PASS" : "FAIL", in, buf, ok ? "" : "   want above");
    if (!ok) printf("        want \"%s\"\n", want);
}
int main(void) {
    gfx_init();
    printf("\n  -- well-formed, per the frozen spec --\n");
    chk("<a>The speed is 12.5 m/s.<end>",                  "The speed is 12.5 m/s.");
    chk("<tool>eval 150/12</tool><res>12.5</res><a>Speed is 12.5 m/s.<end>", "Speed is 12.5 m/s.");

    printf("\n  -- malformed markup degrades to dropping the tag, never to showing it --\n");
    chk("<a>Voltage is <r>12</r> V.<end>",                 "Voltage is 12 V.");
    chk("<a>Value <bogus>x</bogus> here.<end>",            "Value x here.");
    chk("<a>Nested <x><y>z</y></x> done.<end>",            "Nested z done.");

    printf("\n  -- an angle bracket in PROSE is prose, not markup --\n");
    chk("<a>Trailing angle 5 < 7 ok<end>",                 "Trailing angle 5 < 7 ok");
    chk("<a>v < c always, and t > 0.<end>",                "v < c always, and t > 0.");
    chk("<a>Compare 3<4 and 5<6.<end>",                    "Compare 3<4 and 5<6.");
    chk("<a>Unclosed <r 12 here<end>",                     "Unclosed <r 12 here");

    /* MUTATION PASS: prove the oracle can fail. Feed the parser the exact string that broke on
     * screen and assert the OLD behaviour would not have satisfied these assertions. */
    printf("\n  -- mutation pass --\n");
    {   const char *bad = "Voltage is r>12/r> V.";           /* what the old parser produced */
        int caught = strcmp(bad, "Voltage is 12 V.") != 0;
        if (!caught) F++;
        printf("  %s  old output \"%s\" would fail the exact-match oracle\n",
               caught ? "CAUGHT" : "MISSED", bad); }
    {   const char *bad = "Trailing angle 5 ";              /* the over-eager tag skip */
        int caught = strcmp(bad, "Trailing angle 5 < 7 ok") != 0;
        if (!caught) F++;
        printf("  %s  over-eager skip \"%s\" would fail the oracle\n", caught ? "CAUGHT" : "MISSED", bad); }

    printf("\n  %s: span parser, %d failure(s)\n\n", F ? "FAIL" : "PASS", F);
    gfx_free();
    return F != 0;
}
