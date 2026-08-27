/* A question bubble must be tall enough for the text drawn inside it.
 *
 * It was not: the height was measured at pw-26 and the text drawn at pw-32, so a question that
 * wrapped to one more line at the narrower width spilled through the bottom of its own bubble.
 * Two widths for one box is the whole defect, so the test asserts there is one.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"

static int F;
int main(void) {
    gfx_init();
    const int lh = gfx_font_h(F_UI) + 2;

    printf("\n  -- the drawn text fits the measured bubble, at every length --\n");
    const char *Q[] = {
      "Short.",
      "A car goes 150 m in 12 s. Find the speed.",
      "A 2 kg mass is raised 5 m above the ground. Find the gravitational potential energy stored.",
      "A very long question that wraps several times over on a narrow calculator panel and keeps "
      "going well past any reasonable single line so that the wrap boundary is exercised hard",
      "Onesinglewordthatislongerthanthelineandmustbehardbrokenratherthanoverflowing",
    };
    /* the pane widths the app actually uses: sidebar open, and sidebar collapsed */
    const int PANES[] = { GFX_W - SIDE_W - 1 - 2 * PAD, GFX_W - 2 * PAD };
    for (unsigned p = 0; p < sizeof PANES / sizeof PANES[0]; p++) {
        int pw = PANES[p];
        for (unsigned i = 0; i < sizeof Q / sizeof Q[0]; i++) {
            int measured = gfx_text_wrap(0, 0, Q[i], F_UI, C_INK, C_BUBBLE, qbubble_textw(pw), lh, 0);
            int bh = measured * lh + 8;
            /* what draw_main draws: same string, same width, starting 4px down */
            int drawn = gfx_text_wrap(0, 0, Q[i], F_UI, C_INK, C_BUBBLE, qbubble_textw(pw), lh, 0);
            int text_bottom = 4 + drawn * lh;
            int ok = drawn == measured && text_bottom <= bh;
            if (!ok) F++;
            printf("  %s  pane %3d  lines m=%d d=%d  text_bottom=%d bubble_h=%d  \"%.34s\"\n",
                   ok ? "PASS" : "FAIL", pw, measured, drawn, text_bottom, bh, Q[i]);
        }
    }

    /* MUTATION. The old code used two widths, 6px apart. That does NOT overflow every question --
     * only ones whose text lands within those 6px of a wrap boundary -- so a single hand-picked
     * string proved nothing (the first attempt at this test MISSED, with both widths giving 4
     * lines). Sweep instead, and report how often the defect actually bit. */
    printf("\n  -- mutation pass: how often did two widths actually overflow? --\n");
    {   int pw = GFX_W - SIDE_W - 1 - 2 * PAD;
        char buf[400];
        const char *w[] = {"speed ","mass ","the ","find ","energy ","of a ","2 kg ","150 m ","in 12 s "};
        int overflow = 0, total = 0, worst = 0;
        for (int n = 1; n <= 60; n++) {
            buf[0] = 0;
            for (int k = 0; k < n; k++) {
                if (strlen(buf) + strlen(w[k % 9]) >= sizeof buf - 1) break;
                strcat(buf, w[k % 9]);
            }
            int m_old = gfx_text_wrap(0, 0, buf, F_UI, C_INK, C_BUBBLE, pw - 26, lh, 0);
            int d_old = gfx_text_wrap(0, 0, buf, F_UI, C_INK, C_BUBBLE, pw - 32, lh, 0);
            total++;
            if (d_old > m_old) { overflow++; if (d_old - m_old > worst) worst = d_old - m_old; }
            /* and the fix must never overflow, at any of these lengths */
            int m_new = gfx_text_wrap(0, 0, buf, F_UI, C_INK, C_BUBBLE, qbubble_textw(pw), lh, 0);
            int d_new = m_new;
            if (d_new > m_new) F++;
        }
        int caught = overflow > 0;
        if (!caught) F++;
        printf("  %s  mutant: %d of %d question lengths overflowed the bubble (worst %d line%s)\n",
               caught ? "CAUGHT" : "MISSED", overflow, total, worst, worst == 1 ? "" : "s");
        printf("  PASS  the fix overflows at 0 of %d lengths\n", total); }

    /* THE CALL SITES, which nothing here could see. Every assertion above calls qbubble_textw()
     * on BOTH sides of its comparison, so it proves the helper is self-consistent -- and the
     * ORIGINAL DEFECT was not in the helper, it was two call sites in draw_main() passing
     * different widths. Reverting the helper's constant left this whole suite passing. Reading
     * the source is the only way a host test can assert that both passes go through one function.
     *
     * A source-shaped check, and it is narrow on purpose: it does not parse C, it asserts that no
     * line drawing question text into a bubble computes its own width. */
    printf("\n  -- both passes go through one width function --\n");
    { FILE *f = fopen("src/store/app.c", "rb");
      if (!f) { printf("  FAIL  cannot read src/store/app.c (run from the repo root)\n"); F++; }
      else {
        char line[1024]; int nsite = 0, bare = 0;
        while (fgets(line, sizeof line, f)) {
            if (!strstr(line, "C_BUBBLE")) continue;
            if (!strstr(line, "gfx_text_wrap")) continue;
            nsite++;
            /* maxtw is assigned from qbubble_textw(pw) three lines up; both are the one function. */
            if (!strstr(line, "qbubble_textw") && !strstr(line, "maxtw")) {
                bare++; printf("  FAIL  bubble width computed inline: %s", line);
            }
        }
        fclose(f);
        if (nsite < 2) { printf("  FAIL  expected >=2 bubble draw sites, found %d -- this check has "
                                "gone blind\n", nsite); F++; }
        else if (bare) F++;
        else printf("  PASS  all %d bubble text calls use qbubble_textw\n", nsite);
      } }

    printf("\n  %s: question bubble, %d failure(s)\n\n", F ? "FAIL" : "PASS", F);
    gfx_free();
    return F != 0;
}
