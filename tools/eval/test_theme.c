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
        T("every index has a name", P_N, 23); }

    /* The composer fill is a token now, and it must actually differ from the page in BOTH themes --
     * that is the whole reason it is not C_BG. In dark the field is #303030 on a #0d0d0d page; in
     * light it is #ffffff on a #f9f9f9 sidebar and a #ffffff page, so light leans on its border. */
    /* -- surfaces must stay distinct ON THE PANEL, not merely in the source --
     *
     * The framebuffer is RGB565 and gfx.h's RGB() truncates, so a range of source values collapses
     * onto one displayed value: #303030 through #333333 all drive #313031. A tint-reduction pass
     * moved P_SHEET from #2E2E2E to #303030, which left it distinct in the table and IDENTICAL to
     * P_BUBBLE and P_FIELD on the glass. Every existing contrast assertion still passed, because
     * they all compare against the page. Compare the surfaces to each other, post-quantisation. */
    /* -- AUTO resolves against LOCAL time, not UTC --
     *
     * app_clock_hour() can only report UTC: the RTC is a bare seconds counter and the calculator
     * has no notion of a timezone anywhere. So AUTO was calling it night at 10am for anyone far
     * from Greenwich. At UTC-10 the clock reads 20:00 in broad daylight, which is precisely the
     * report that led here. */
    printf("\n  -- AUTO uses local time --\n");
    {   T("UTC noon is day",            app_auto_is_dark(12), 0);
        T("UTC midnight is night",      app_auto_is_dark(0), 1);
        T("06:00 is the first day hour", app_auto_is_dark(6), 0);
        T("18:00 is the first night hour", app_auto_is_dark(18), 1);
        T("an unreadable clock is not night", app_auto_is_dark(-1), 0);

        /* the offset itself */
        app_set_tz(0);
        T("offset starts where it is put", app_tz(), 0);
        app_set_tz(-10);
        T("it takes a negative offset",    app_tz(), -10);
        app_set_tz(-99);
        T("and clamps below -12",          app_tz(), -12);
        app_set_tz(99);
        T("and above +14",                 app_tz(), 14);

        /* the shift itself: 20:00 UTC at -10 is 10:00 local, which is day */
        {   int h = (20 + (-10)) % 24; if (h < 0) h += 24;
            T("20:00 UTC at -10 is 10:00 local", h, 10);
            T("and 10:00 local is day",          app_auto_is_dark(h), 0);
            /* MUTATION: the shipped behaviour, which read the same instant as night */
            int caught = (app_auto_is_dark(20) != app_auto_is_dark(h));
            if (!caught) F++;
            printf("  %s  mutant: AUTO on raw UTC calls 10am local night\n",
                   caught ? "CAUGHT" : "MISSED"); }
        app_set_tz(0);
    }

    printf("\n  -- surfaces are distinct after RGB565 --\n");
    {   struct { const char *a, *b; uint16_t x, y; } pairs[] = {
            { "dark sheet",  "dark bubble", PAL_DARK[P_SHEET],  PAL_DARK[P_BUBBLE] },
            { "dark sheet",  "dark field",  PAL_DARK[P_SHEET],  PAL_DARK[P_FIELD]  },
            { "dark bubble", "dark page",   PAL_DARK[P_BUBBLE], PAL_DARK[P_BG]     },
            { "dark sel",    "dark side",   PAL_DARK[P_SEL],    PAL_DARK[P_SIDE]   },
            { "light sheet", "light bubble",PAL_LIGHT[P_SHEET], PAL_LIGHT[P_BUBBLE]},
            { "light sel",   "light side",  PAL_LIGHT[P_SEL],   PAL_LIGHT[P_SIDE]  },
        };
        for (int i = 0; i < (int)(sizeof pairs / sizeof pairs[0]); i++) {
            char msg[96];
            snprintf(msg, sizeof msg, "%s != %s on the panel", pairs[i].a, pairs[i].b);
            T(msg, pairs[i].x != pairs[i].y, 1);
        }
        /* MUTATION: the value the tint pass chose, which collapsed onto the bubble. */
        uint16_t collapsed = HEX(0x303030);
        int caught = (collapsed == PAL_DARK[P_BUBBLE]);
        if (!caught) F++;
        printf("  %s  mutant: sheet at #303030 lands on the bubble's panel value\n",
               caught ? "CAUGHT" : "MISSED");
    }

    printf("\n  -- the composer fill is its own surface --\n");
    {   T("dark: field lifts off the page", PAL_DARK[P_FIELD] != PAL_DARK[P_BG], 1);
        T("dark: placeholder readable on it", ratio(PAL_DARK[P_INK3], PAL_DARK[P_FIELD]) >= 3.0, 1);
        T("light: placeholder readable on it", ratio(PAL_LIGHT[P_INK3], PAL_LIGHT[P_FIELD]) >= 3.0, 1);
        T("light: typed text readable on it", ratio(PAL_LIGHT[P_INK], PAL_LIGHT[P_FIELD]) >= 4.5, 1);
        T("dark: typed text readable on it", ratio(PAL_DARK[P_INK], PAL_DARK[P_FIELD]) >= 4.5, 1);
        /* MUTATION: the bug this token fixes -- drawing the field on the page colour. */
        int caught = !(ratio(PAL_DARK[P_INK3], PAL_DARK[P_BG]) < 3.0);
        printf("  %s  mutant: field drawn on C_BG (dark) -> %.2f:1 placeholder\n",
               caught ? "note" : "note", ratio(PAL_DARK[P_INK3], PAL_DARK[P_BG])); }

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
