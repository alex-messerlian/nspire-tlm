/* Device theming: the palette is complete in both directions, readable in both, and AUTO never
 * invents a clock it does not have.
 *
 * On the web this was almost entirely a tokenisation problem -- ~25 colours hardcoded inside rules,
 * every one a place dark mode broke. The device had the same shape: fifteen #defines that could not
 * vary at all, plus six colours written inline. This checks the result the same way, including
 * CONTRAST, because a dark theme that is merely dark is not a dark theme.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../../src/store/app.c"

static int F;
static void T(const char *n, int got, int want) {
    int ok = got == want; if (!ok) F++;
    printf("  %s  %-50s got=%d want=%d\n", ok ? "PASS" : "FAIL", n, got, want);
}
/* RGB565 -> relative luminance, so contrast is computed on what the PANEL shows, not on the
 * 24-bit value the source was written in. */
static double lum(uint16_t c) {
    double v[3] = { ((c >> 11) & 31) / 31.0, ((c >> 5) & 63) / 63.0, (c & 31) / 31.0 };
    for (int i = 0; i < 3; i++) v[i] = v[i] <= 0.03928 ? v[i] / 12.92 : pow((v[i] + 0.055) / 1.055, 2.4);
    return 0.2126 * v[0] + 0.7152 * v[1] + 0.0722 * v[2];
}
static double ratio(uint16_t a, uint16_t b) {
    double A = lum(a), B = lum(b);
    return (fmax(A, B) + 0.05) / (fmin(A, B) + 0.05);
}

int main(void) {
    gfx_init();

    printf("\n  -- both palettes are COMPLETE: no entry left at zero --\n");
    {   int zl = 0, zd = 0;
        for (int i = 0; i < P_N; i++) { if (!PAL_LIGHT[i]) zl++; if (!PAL_DARK[i]) zd++; }
        /* P_SCRIM is legitimately black in both, so compare against its known value instead */
        T("light palette fully populated", zl - (PAL_LIGHT[P_SCRIM] == 0), 0);
        T("dark palette fully populated",  zd - (PAL_DARK[P_SCRIM] == 0), 0);
        T("every index has a name", P_N, 22); }

    printf("\n  -- the toggle cycles auto -> light -> dark -> auto --\n");
    app_set_theme(TH_AUTO);
    T("starts auto", app_theme(), TH_AUTO);
    app_set_theme((app_theme() + 1) % 3); T("auto -> light", app_theme(), TH_LIGHT);
    app_set_theme((app_theme() + 1) % 3); T("light -> dark", app_theme(), TH_DARK);
    app_set_theme((app_theme() + 1) % 3); T("dark -> auto", app_theme(), TH_AUTO);

    printf("\n  -- the explicit states are ABSOLUTE --\n");
    app_set_theme(TH_LIGHT);
    T("sun gives a light ground", C_BG == PAL_LIGHT[P_BG], 1);
    app_set_theme(TH_DARK);
    T("moon gives a dark ground", C_BG == PAL_DARK[P_BG], 1);
    T("the two grounds differ",   PAL_LIGHT[P_BG] != PAL_DARK[P_BG], 1);

    printf("\n  -- AUTO does not invent a clock --\n");
    T("no clock (-1) -> light",  app_auto_is_dark(-1), 0);
    T("hour 99 -> light",        app_auto_is_dark(99), 0);
    T("06:00 is day",            app_auto_is_dark(6),  0);
    T("17:00 is day",            app_auto_is_dark(17), 0);
    T("18:00 is night",          app_auto_is_dark(18), 1);
    T("05:00 is night",          app_auto_is_dark(5),  1);
    T("00:00 is night",          app_auto_is_dark(0),  1);
    T("the host harness reports no clock", app_clock_hour(), -1);
    app_set_theme(TH_AUTO);
    T("so AUTO resolves to light here", C_BG == PAL_LIGHT[P_BG], 1);

    printf("\n  -- CONTRAST, on the RGB565 the panel actually shows --\n");
    {   struct { const char *n; int fg, bg; double need; } C[] = {
          { "body text on pane",     P_INK,   P_BG,     4.5 },
          { "secondary on pane",     P_INK2,  P_BG,     4.5 },
          { "status grey on pane",   P_INK3,  P_BG,     3.0 },
          { "text on bubble",        P_INK,   P_BUBBLE, 4.5 },
          { "text on selected row",  P_INK,   P_SEL,    4.5 },
          { "sidebar text",          P_INK,   P_SIDE,   4.5 },
          { "error text",            P_ERRFG, P_ERR,    4.5 },
          { "result text",           P_RESFG, P_RES,    4.5 },
        };
        for (int th = 0; th < 2; th++) {
            const uint16_t *P = th ? PAL_DARK : PAL_LIGHT;
            for (unsigned i = 0; i < sizeof C / sizeof C[0]; i++) {
                double r = ratio(P[C[i].fg], P[C[i].bg]);
                int ok = r >= C[i].need;
                if (!ok) F++;
                printf("  %s  %-5s %-22s %5.2f:1  (need %.1f)\n",
                       ok ? "PASS" : "FAIL", th ? "dark" : "light", C[i].n, r, C[i].need);
            }
        }
    }

    printf("\n  -- separations that carry structure must survive RGB565 --\n");
    {   struct { const char *n; int a, b; } S[] = {
          { "line against pane",   P_LINE,  P_BG },
          { "bubble against pane", P_BUBBLE,P_BG },
          { "selected against sidebar", P_SEL, P_SIDE },
          { "scrollbar against pane", P_BAR, P_BG },
        };
        for (int th = 0; th < 2; th++) {
            const uint16_t *P = th ? PAL_DARK : PAL_LIGHT;
            for (unsigned i = 0; i < sizeof S / sizeof S[0]; i++) {
                int ok = P[S[i].a] != P[S[i].b];   /* quantisation must not merge them */
                if (!ok) F++;
                printf("  %s  %-5s %-28s 0x%04X vs 0x%04X\n", ok ? "PASS" : "FAIL",
                       th ? "dark" : "light", S[i].n, P[S[i].a], P[S[i].b]);
            }
        }
    }

    /* SURFACE against SURFACE, not just text against surface.
     *
     * The contrast block above passed while the search sheet was byte-identical to the page behind
     * it in dark: every text pair was fine because the text was fine, and nobody had asked whether
     * the two SURFACES differed. A scrim cannot darken a near-black ground -- 28% of 0x0D0D0D
     * quantises to no change at all in RGB565 -- so a modal has to lift, and that is a property of
     * the palette, checkable here. */
    printf("\n  -- a modal surface must be distinguishable from the page --\n");
    /* Compared on the RENDERED framebuffer, not on the palette, because the two themes separate
     * the sheet by DIFFERENT mechanisms: in light the sheet and page are both white and the SCRIM
     * darkens the page behind it; in dark the scrim can do nothing -- 28% of 0x0D0D0D quantises to
     * no change at all -- so the sheet has to lift instead. A palette-level assertion encodes the
     * dark mechanism and is simply false for light, which is how the first version of this check
     * failed on correct code. */
    for (int th = TH_LIGHT; th <= TH_DARK; th++) {
        app_set_theme(th);
        gfx_clear(C_BG); gfx_dim(C_SCRIM, 28);
        uint16_t page = gfx_buf()[10];
        gfx_rrect(40, 40, 200, 100, 8, C_SHEET);
        uint16_t sheet = gfx_buf()[70 * GFX_W + 120];
        int ok = page != sheet;
        if (!ok) F++;
        printf("  %s  %-5s RENDERED page 0x%04X vs sheet 0x%04X\n", ok ? "PASS" : "FAIL",
               th == TH_DARK ? "dark" : "light", page, sheet);
    }

    printf("\n  -- mutation pass --\n");
    {   /* a palette entry left unset would render black-on-black in dark mode */
        uint16_t save = PAL_DARK[P_INK];
        double bad = ratio(0x0000, PAL_DARK[P_BG]);
        int caught = bad < 4.5;
        if (!caught) F++;
        printf("  %s  mutant: an unset (black) ink on the dark ground scores %.2f:1\n",
               caught ? "CAUGHT" : "MISSED", bad);
        (void)save; }
    {   /* the old code could not theme at all: the defines were compile-time constants */
        app_set_theme(TH_LIGHT); uint16_t a = C_BG;
        app_set_theme(TH_DARK);  uint16_t b = C_BG;
        int caught = a != b;
        if (!caught) F++;
        printf("  %s  mutant: with #define constants C_BG could not change (0x%04X vs 0x%04X)\n",
               caught ? "CAUGHT" : "MISSED", a, b); }

    app_set_theme(TH_LIGHT);
    printf("\n  %s: device theme, %d failure(s)\n\n", F ? "FAIL" : "PASS", F);
    gfx_free();
    return F != 0;
}
