/* Display notation: correct output, and -- the part that actually matters -- every glyph it maps to
 * must EXIST in the font.
 *
 * gfx_text draws nothing at all for a missing glyph. Not a box, not a question mark: nothing. So a
 * map pointing at a code point the font lacks does not look broken, it makes characters silently
 * disappear from the answer. That is the failure this file exists to prevent, and it is checked in
 * all three faces because the answer, its title and its labels do not use the same one.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"

static int F;
static void T(const char *in, const char *want) {
    char got[640];
    to_display(in, got, sizeof got);
    int ok = strcmp(got, want) == 0;
    if (!ok) F++;
    printf("  %s  %-42s -> %s\n", ok ? "PASS" : "FAIL", in, got);
    if (!ok) printf("        want %s\n", want);
}

/* decode one UTF-8 code point */
static const char *cp(const char *s, unsigned *out) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) { *out = c; return s + 1; }
    if ((c & 0xE0) == 0xC0) { *out = ((c & 0x1Fu) << 6) | (s[1] & 0x3Fu); return s + 2; }
    if ((c & 0xF0) == 0xE0) { *out = ((c & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu); return s + 3; }
    *out = '?'; return s + 1;
}

int main(void) {
    gfx_init();

    printf("\n  -- formulas as the corpus actually writes them --\n");
    T("Delta_p=m*Delta_v",        "\xce\x94_p=m*\xce\x94_v");
    T("omega=sqrt((k)/(m))",      "\xcf\x89=\xe2\x88\x9a((k)/(m))");
    T("lambda=((h*c)/(E))",       "\xce\xbb=((h*c)/(E))");
    T("I_0=((V_0)/(Z))",          "I\xe2\x82\x80=((V\xe2\x82\x80)/(Z))");
    T("f_beat=|f_2-f_1|",         "f_beat=|f\xe2\x82\x82-f\xe2\x82\x81|");
    T("theta_image",              "\xce\xb8_image");
    T("E=m*c^2",                  "E=m*c\xc2\xb2");
    T("f=((1)/(2*pi))",           "f=((1)/(2*\xcf\x80))");
    T("a_CM=R*alpha",             "a_CM=R*\xce\xb1");
    T("rho, epsilon, tau, mu",    "\xcf\x81, \xce\xb5, \xcf\x84, \xce\xbc");

    printf("\n  -- word boundaries: a name inside a word is NOT a symbol --\n");
    T("spin",                     "spin");          /* not "s" + pi + "n" */
    T("theta",                    "\xce\xb8");      /* and "eta" inside it must not win */
    T("meta data",                "meta data");
    T("The speed is 12.5 m/s.",   "The speed is 12.5 m/s.");
    T("capacity",                 "capacity");
    T("delta and Delta",          "\xce\xb4 and \xce\x94");

    printf("\n  -- EVERY glyph this maps to exists, in all three faces --\n");
    {   int missing = 0, checked = 0;
        const gfx_font faces[3] = { F_UI, F_UIB, F_BIG };
        const char *names[3] = { "F_UI", "F_UIB", "F_BIG" };
        for (unsigned s = 0; s < sizeof SYMS / sizeof SYMS[0]; s++)
            for (int f = 0; f < 3; f++) {
                unsigned c; cp(SYMS[s].to, &c); checked++;
                char one[8]; snprintf(one, sizeof one, "%s", SYMS[s].to);
                if (gfx_text_w(one, faces[f]) == 0) {
                    printf("  FAIL  %s -> U+%04X absent from %s (would VANISH)\n",
                           SYMS[s].from, c, names[f]); missing++; }
            }
        for (int d = 0; d < 10; d++)
            for (int f = 0; f < 3; f++) {
                checked += 2;
                if (gfx_text_w(SUB[d], faces[f]) == 0) { printf("  FAIL  subscript %d absent from %s\n", d, names[f]); missing++; }
                if (gfx_text_w(SUP[d], faces[f]) == 0) { printf("  FAIL  superscript %d absent from %s\n", d, names[f]); missing++; }
            }
        if (missing) F += missing;
        printf("  %s  %d glyph/face pairs checked, %d absent\n",
               missing ? "FAIL" : "PASS", checked, missing); }

    /* NO GLYPH MAY BE A TOFU BOX.
     *
     * The first version of this file checked gfx_text_w() != 0 and passed on 18 glyphs that were
     * the missing-glyph BOX -- present in the index, inked, the right width, and meaningless. On
     * screen "lambda_0" rendered as "lambda-box". Width and ink are both satisfied by a box, so the
     * only test that works compares PIXELS between two different code points: two distinct
     * characters that render identically are both the substitute. */
    printf("\n  -- no mapped glyph is the missing-glyph box --\n");
    {   static uint16_t ref[GFX_W * 26];
        int boxes = 0, checked = 0;
        const gfx_font faces[3] = { F_UI, F_UIB, F_BIG };
        const char *fnames[3] = { "F_UI", "F_UIB", "F_BIG" };
        for (int fi = 0; fi < 3; fi++) {
            /* reference: draw the FIRST subscript, then compare every other mapped glyph to it.
             * Any two that match are the same substitute shape. */
            gfx_clear(HEX(0xFFFFFF));
            gfx_text(2, 2, SUB[0], faces[fi], HEX(0x000000), HEX(0xFFFFFF));
            memcpy(ref, gfx_buf(), sizeof ref);
            for (int d = 1; d < 10; d++) {
                gfx_clear(HEX(0xFFFFFF));
                gfx_text(2, 2, SUB[d], faces[fi], HEX(0x000000), HEX(0xFFFFFF));
                checked++;
                if (memcmp(ref, gfx_buf(), sizeof ref) == 0) {
                    printf("  FAIL  %s subscript %d is the same shape as subscript 0 (tofu)\n", fnames[fi], d);
                    boxes++; }
            }
            gfx_clear(HEX(0xFFFFFF));
            gfx_text(2, 2, SUP[0], faces[fi], HEX(0x000000), HEX(0xFFFFFF));
            memcpy(ref, gfx_buf(), sizeof ref);
            for (int d = 1; d < 10; d++) {
                gfx_clear(HEX(0xFFFFFF));
                gfx_text(2, 2, SUP[d], faces[fi], HEX(0x000000), HEX(0xFFFFFF));
                checked++;
                if (memcmp(ref, gfx_buf(), sizeof ref) == 0) {
                    printf("  FAIL  %s superscript %d is the same shape as superscript 0 (tofu)\n", fnames[fi], d);
                    boxes++; }
            }
        }
        if (boxes) F += boxes;
        printf("  %s  %d distinct-shape comparisons, %d box(es)\n",
               boxes ? "FAIL" : "PASS", checked, boxes); }

    printf("\n  -- mutation pass --\n");
    {   /* a map entry pointing at a glyph the font lacks: U+2103 is not in any face */
        int w = gfx_text_w("\xe2\x84\x83", F_UI);
        int caught = (w == 0);
        if (!caught) F++;
        printf("  %s  mutant: an absent glyph measures 0 wide, i.e. it would vanish silently\n",
               caught ? "CAUGHT" : "MISSED"); }
    {   char got[64]; to_display("spin", got, sizeof got);
        int caught = strcmp(got, "spin") == 0;
        if (!caught) F++;
        printf("  %s  mutant: without word boundaries \"spin\" would become s\xcf\x80n\n",
               caught ? "CAUGHT" : "MISSED"); }
    {   char got[8]; to_display("Delta_p", got, sizeof got);   /* cap smaller than the output */
        int caught = strlen(got) < sizeof got;
        if (!caught) F++;
        printf("  %s  mutant: a tight buffer truncates rather than overruns (\"%s\")\n",
               caught ? "CAUGHT" : "MISSED", got); }

    printf("\n  %s: display notation, %d failure(s)\n\n", F ? "FAIL" : "PASS", F);
    gfx_free();
    return F != 0;
}
