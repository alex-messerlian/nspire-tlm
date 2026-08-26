#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "app.h"
#include "chatstore.h"

/* ---- state ---------------------------------------------------------------------------------- */
static app_chat CHATS[MAX_CHATS];
static int NCHATS, CUR = -1;
static int SCROLL;                 /* transcript scroll offset in pixels */
static int MX = 160, MY = 120;     /* cursor */
static int HOVER;                  /* pointer in proximity: hover states are live */
static int QUIT;
static int SIDEBAR = 1;
uint16_t TLM_PAL[P_N];
static int THEME_MODE = TH_AUTO;

/* Light is the calculator's own register: the Nspire OS is light-only, so this is what a person
 * expects to see when they open a program on it. */
static const uint16_t PAL_LIGHT[P_N] = {
    [P_BG] = HEX(0xFFFFFF), [P_SIDE] = HEX(0xFFFFFF), [P_LINE] = HEX(0xE5E5E5),
    [P_INK] = HEX(0x0B0B0B), [P_INK2] = HEX(0x5B5B5B), [P_INK3] = HEX(0x8F8F8F),
    [P_BUBBLE] = HEX(0xF4F4F4), [P_SEL] = HEX(0xECECEC),
    [P_TOOL] = HEX(0xF5F6F8), [P_TOOLLN] = HEX(0xE3E5EA),
    [P_RES] = HEX(0xEDF7F0), [P_RESLN] = HEX(0xCFE8D8), [P_RESFG] = HEX(0x186A3B),
    [P_ERR] = HEX(0xFDF2F2), [P_ERRFG] = HEX(0xA8342C),
    [P_SCRIM] = HEX(0x000000), [P_TRASH_HOT] = HEX(0xE6E6E6), [P_BAR] = HEX(0xEDEDED),
    [P_EXIT_HOT] = HEX(0xF3D9D7), [P_FIELD] = HEX(0xFFFFFF), [P_FIELD_LN] = HEX(0xD7D7D7), [P_SEND_OFF] = HEX(0xD5D5D5),
    [P_SHEET] = HEX(0xFFFFFF),   /* white on a dimmed page */
};
/* Dark is not inverted light. Surfaces LIFT as they come forward, as on the web -- and this panel
 * is 16-bit, so a near-black ground has only a few distinguishable steps above it before the
 * quantisation shows. The steps below are chosen far enough apart to survive RGB565. */
static const uint16_t PAL_DARK[P_N] = {
    [P_BG] = HEX(0x0B0B0B), [P_SIDE] = HEX(0x0B0B0B), [P_LINE] = HEX(0x2A2A2A),
    [P_INK] = HEX(0xECECEC), [P_INK2] = HEX(0xAFAFAF), [P_INK3] = HEX(0x8A8A8A),
    [P_BUBBLE] = HEX(0x303030), [P_SEL] = HEX(0x232323),
    [P_TOOL] = HEX(0x22262E), [P_TOOLLN] = HEX(0x333A45),
    [P_RES] = HEX(0x16281D), [P_RESLN] = HEX(0x27492F), [P_RESFG] = HEX(0x79D497),
    [P_ERR] = HEX(0x2C1B1B), [P_ERRFG] = HEX(0xF0857C),
    [P_SCRIM] = HEX(0x000000), [P_TRASH_HOT] = HEX(0x3A3A3A), [P_BAR] = HEX(0x333333),
    [P_EXIT_HOT] = HEX(0x4A2A28), [P_FIELD] = HEX(0x303030), [P_FIELD_LN] = HEX(0x303030), [P_SEND_OFF] = HEX(0x3B3B3B),
    [P_SHEET] = HEX(0x383838),   /* lifted OFF the page, since the scrim cannot sink it */
};

/* The clock decides only when the mode is AUTO. A negative hour means the clock could not be read;
 * light is returned then, because a wrong-but-legible default beats guessing dark on no evidence.
 * Day is 06:00-18:00, per the owner's spec. */
int app_auto_is_dark(int hour) {
    if (hour < 0 || hour > 23) return 0;
    return !(hour >= 6 && hour < 18);
}
int app_theme(void) { return THEME_MODE; }

void app_set_theme(int mode) {
    THEME_MODE = mode;
    int dark = (mode == TH_DARK);
    if (mode == TH_AUTO) dark = app_auto_is_dark(app_clock_hour());
    const uint16_t *src = dark ? PAL_DARK : PAL_LIGHT;
    for (int i = 0; i < P_N; i++) TLM_PAL[i] = src[i];
}

/* Deliberately phrased as a person would ask, not as the store phrases a relation -- these are
 * examples of USE, and they have to still make sense once the relation flow is gone. */

static const char *PERSIST;          /* NULL = do not persist (host harness) */

/* Called after EVERY change that could lose a conversation. Deliberately not called per token:
 * writing 141 KB at 2.68 tok/s would dominate generation, and a turn in progress is not worth
 * saving anyway -- it is the finished ones that matter. */
static void persist(void) {
    if (PERSIST) chat_save(PERSIST, CHATS, NCHATS, CUR);
}
void app_set_persist(const char *path) {
    PERSIST = path;
    if (!path) return;
    int cur = -1;
    int n = chat_load(path, CHATS, MAX_CHATS, &cur);
    if (n > 0) { NCHATS = n; CUR = cur; }
}
static char COMPOSE[160];          /* what is being typed */
static int  COMPOSE_N;
static int  BUSY;

/* live status, drawn above the streaming answer */
static char STATUS[48], STATUS_MONO[40];

/* hit regions, recomputed every frame so hover testing and click handling can never disagree
 * about where something is -- they read the same rectangles. */
static gfx_rect R_TOGGLE, R_NEW, R_CHAT[MAX_CHATS], R_TRASH[MAX_CHATS], R_FIELD, R_SEND;
static gfx_rect R_EXIT;
/* Hover marquee for session titles.
 *
 * Titles are ellipsised at ~64px, which for a question is a few words -- often not enough to tell
 * two sessions apart. On hover the full title scrolls left, stops at its end and stays there;
 * moving away resets it. MARQ_AT is the chat index under the cursor and MARQ_T counts draws since
 * it arrived, so the animation is driven by the redraw loop and needs no clock. */
/* The app's millisecond clock, fed by app_set_now(). Declared ahead of BOTH animations because
 * each reads it, and the marquee's helper sits above the placeholder's table. */
static unsigned NOW_MS;

static int SEL_ROW;          /* keyboard selection in the session list */
static int MARQ_AT = -1;
static unsigned MARQ_T0;              /* NOW_MS when the cursor arrived on this row */
#define MARQ_HOLD_MS 420      /* wait before moving, so a pass-through does not twitch */
#define MARQ_PX_S    38       /* pixels per second of travel */

/* Travel after `t` draws, for a title overflowing its band by `overflow` px. Its own function so
 * the clamp can be tested directly: without it the title scrolls off its own left edge and the row
 * ends up blank, which looks like a rendering fault rather than a missing bound. */
/* Elapsed since the cursor arrived, clamped at zero. NOW_MS - MARQ_T0 is UNSIGNED: if the clock
 * ever runs backwards -- a wrap of the 32 kHz counter, or a test setting the clock behind a stamp
 * left by an earlier case -- the difference becomes billions and the title snaps to full travel.
 * Seen exactly once, in a test, which is the cheap place to see it. */
static unsigned marq_elapsed(void) { return NOW_MS >= MARQ_T0 ? NOW_MS - MARQ_T0 : 0u; }

static int marq_off(unsigned elapsed_ms, int overflow) {
    if (overflow <= 0 || elapsed_ms <= MARQ_HOLD_MS) return 0;
    int off = (int)(((elapsed_ms - MARQ_HOLD_MS) * MARQ_PX_S) / 1000u);
    return off > overflow ? overflow : off;
}
static void draw_composer(int x0, int w, int cy);

/* THE COMPOSER GROWS UPWARD, like the web one and like every chat box people already use.
 *
 * It used to be a fixed 24px slot, so a question longer than one line spilled out of the pill and
 * over whatever was beneath it. Now the field takes as many lines as the text needs, the dock
 * grows with it, and the transcript's bottom rises to match -- the field's BOTTOM edge never moves.
 *
 * Capped at four lines. Past that the text scrolls inside the field and the oldest lines stop
 * being shown, which is the answer to "should there be a character limit": there is no limit on
 * what you can type, only on how much of it is on screen at once. A hard cap would silently
 * refuse keystrokes, and a composer that ignores the keypad is worse than one that scrolls. */
/* THE PLACEHOLDER NAMES WHAT THIS THING IS FOR.
 *
 * "Ask ChatTLM" told a reader the app's name, which the title bar already did, and nothing about
 * what it can answer -- on a calculator handed to someone cold, that is the whole question. The
 * subject rotates instead, and the word slides up out of the line while the next slides in, so the
 * change reads as one control changing its mind rather than as text being redrawn.
 *
 * The prefix does not move. Only the word travels, clipped to the line box, which is what makes it
 * legible at 15px: two strings crossing in a 15px window is already the most motion this panel can
 * carry without smearing.
 *
 * Driven by the draw loop like the title marquee, so it needs no clock and stops when the app
 * stops drawing. */
/* WIDE ON PURPOSE, and not limited to what the store answers today. The brief scopes this to
 * algebra, calculus, physics and statistics; the corpus and the checkpoint catch up to that, and a
 * placeholder that only ever offered the six subjects the current 166 records happen to cover
 * would be designing the product down to a temporary dataset.
 *
 * Every entry is checked to FIT: the prefix is 83 px and the field's text budget is 190 px, so a
 * subject has 107 px. All 39 clear it, "standard deviation" being the longest. */
/* Which subject is showing during cycle `n`. SHUFFLED rather than in table order: reading the same
 * 39 words in the same sequence every time makes it a list being recited, and the point is that the
 * app answers a wide range of things, not that it has a particular order.
 *
 * It is a hash of the cycle number, not a running RNG, so it stays a pure function of the clock:
 * the same instant always shows the same word, which is what makes it testable and what stops a
 * redraw from advancing it. Consecutive repeats are stepped past, because the same word appearing
 * twice in a row looks like the animation broke. */
static int ask_at(unsigned n);
static int ask_index(unsigned n);

static const char *ASK_ABOUT[] = {
    "motion", "velocity", "acceleration", "forces",
    "friction", "momentum", "energy", "work",
    "power", "gravity", "circular motion", "waves",
    "sound", "light", "optics", "heat",
    "pressure", "electricity", "circuits", "magnetism",
    "algebra", "geometry", "trigonometry", "logarithms",
    "vectors", "matrices", "sequences", "series",
    "quadratics", "exponentials", "derivatives", "integrals",
    "limits", "optimization", "probability", "averages",
    "distributions", "regression",
};
#define ASK_N     ((int)(sizeof ASK_ABOUT / sizeof ASK_ABOUT[0]))

static int ask_at(unsigned n) {
    unsigned h = (n + 1u) * 2654435761u;      /* Knuth's multiplicative hash */
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    return (int)(h % (unsigned)ASK_N);
}
static int ask_index(unsigned n) {
    int i = ask_at(n);
    if (n && i == ask_at(n - 1u)) i = (i + 1) % ASK_N;   /* never the same word twice running */
    return i;
}
/* MILLISECONDS, not draws.
 *
 * These were frame counts, and app_draw() runs only when there is input -- so the rotation raced
 * while a finger was on the pad, because every touchpad sample forced a redraw, and froze the
 * instant the finger came off. A frame count is only a clock when something guarantees the frame
 * rate, and nothing here did.
 *
 * app_set_now() is fed the device's 32 kHz timer. On the host it is fed a counter, so the tests
 * stay deterministic. */
#define ASK_HOLD_MS  4200     /* a word rests this long */
#define ASK_SLIDE_MS  420     /* and takes this long to cross */


#define COMPOSE_MAX_LINES 4
#define COMPOSE_LH        15     /* F_UI's box */

/* Text width inside the field, for a pane of width `w`. ONE definition, because a measure/draw
 * mismatch here is exactly the bubble-width defect that took a 60-case sweep to find. */
static int compose_textw(int w) { return w - 2 * PAD - 9 - 24; }

/* Lines the composer wants, before the cap, for a pane of width `w`. */
static int compose_lines_total(int w) {
    if (!COMPOSE_N) return 1;
    int n = gfx_text_wrap(0, 0, COMPOSE, F_UI, C_INK, C_FIELD, compose_textw(w), COMPOSE_LH, 0);
    return n < 1 ? 1 : n;
}
static int compose_lines(int w) {
    int n = compose_lines_total(w);
    return n > COMPOSE_MAX_LINES ? COMPOSE_MAX_LINES : n;
}
static int compose_field_h(int w) { return compose_lines(w) * COMPOSE_LH + 9; }
/* The dock is the field plus 3px above, then 2px and the 13px disclaimer below. */
static int compose_dock_h(int w) { return compose_field_h(w) + 3 + 3 + 11 + 2; }
static int EMPTY_COMPOSER;   /* set per-frame: the composer was drawn centred, so do not dock it */              /* always visible: leaving must not depend on knowing a key */
static int ABORT;                    /* set by ESC or Stop; polled by the generation loop */
static int NCHAT_ROWS;
static int CHAT_SCROLL;              /* index of the first chat row drawn */
static int CHAT_AT[MAX_CHATS];       /* screen row -> chat index */
static int CHAT_FIT;                 /* rows that fit, recomputed each frame */
static gfx_rect R_LIST;              /* the scrollable list area, for hit-testing the wheel */

static void clamp_scroll(int content_h, int view_h);

/* HIT TESTING IS MORE FORGIVING THAN DRAWING.
 *
 * A 24px plate is a small target for a cursor driven by a 2 cm pad, and requiring the pointer to
 * land inside the drawn box means near-misses do nothing at all. The rect a control is DRAWN in
 * and the rect it RESPONDS to are different things, and only the first one needs to be exact.
 *
 * 5px on every side. Chosen so the three icons in the sidebar band, which sit on a 28px pitch,
 * gain reach without their boxes meeting: 24 + 5 + 5 = 34 would overlap, so the helper also stops
 * short of the midpoint between neighbours by clamping to the pitch. */
#define HIT_PAD 5

static int inside_pad(gfx_rect r, int x, int y, int pad) {
    return x >= r.x - pad && x < r.x + r.w + pad &&
           y >= r.y - pad && y < r.y + r.h + pad;
}
static int hit(gfx_rect r, int x, int y) { return inside_pad(r, x, y, HIT_PAD); }

static int inside(gfx_rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void app_init(void) {
    SEL_ROW = 0;
    app_set_theme(THEME_MODE);   /* fill the palette before anything draws */
    NCHATS = 0; CUR = -1; SCROLL = 0; COMPOSE[0] = 0; COMPOSE_N = 0;
}
int app_should_quit(void) { return QUIT; }

/* INDEX 0 IS NEWEST. touch_chat() and draw_sidebar() have always assumed that; new_chat did not.
 *
 * It appended at CHATS[NCHATS], so a brand-new session appeared at the BOTTOM of a list headed
 * "Recents" -- and its eviction dropped CHATS[0], which under that same convention is the MOST
 * recently used session, not the oldest, directly contradicting its own comment. Once sessions
 * persisted to flash, a thirteenth chat silently and permanently deleted the one just demonstrated.
 *
 * Two conventions for one array, one of them written down and the other not. */
static app_chat *new_chat(const char *title) {
    if (NCHATS >= MAX_CHATS) NCHATS = MAX_CHATS - 1;   /* drop the LAST: least recently used */
    memmove(&CHATS[1], &CHATS[0], sizeof(app_chat) * (size_t)NCHATS);
    app_chat *c = &CHATS[0];
    memset(c, 0, sizeof *c);
    snprintf(c->title, sizeof c->title, "%s", title);
    c->used = 1;
    CUR = 0;
    NCHATS++;
    CHAT_SCROLL = 0;                    /* the new session is at the top: show it */
    return c;
}
static void delete_chat(int i) {
    /* saved at the end of this function */
    if (i < 0 || i >= NCHATS) return;
    memset(&CHATS[i], 0, sizeof(app_chat));      /* wiped, not just unlinked */
    memmove(&CHATS[i], &CHATS[i + 1], sizeof(app_chat) * (NCHATS - i - 1));
    NCHATS--;
    if (CUR == i) CUR = -1; else if (CUR > i) CUR--;
    persist();                       /* a deletion must not come back on the next run */
}

/* ---- span markup ------------------------------------------------------------------------------
 * The model emits <tool>eval<arg>EXPR</tool><res>V</res><a>PROSE<end>. Rendering walks the string
 * once and draws each span in its own style -- there is no intermediate parse tree, because the
 * text is being appended token by token while it streams and a tree would be rebuilt every frame. */
typedef enum { SP_TOOL, SP_RES, SP_TEXT } sp_kind;

/* Is this '<' the start of markup, or just a less-than in prose? "<name>" / "</name>" with a short
 * alphanumeric name is markup; "5 < 7" is not. Without this test an inequality in an answer ate the
 * rest of the sentence, which for a PHYSICS model is not a hypothetical input. */
static int istag(const char *e) {
    const char *p = e + 1;
    if (*p == '/') p++;
    const char *nm = p;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) p++;
    return p > nm && *p == '>' && (p - nm) <= 10;
}

static const char *span_next(const char *s, sp_kind *k, char *out, int cap) {
    if (!*s) return 0;
    if (!strncmp(s, "<tool>", 6)) {
        const char *e = strstr(s, "</tool>");
        if (!e) e = s + strlen(s);
        int n = (int)(e - s - 6); if (n >= cap) n = cap - 1;
        memcpy(out, s + 6, (size_t)n); out[n] = 0;
        *k = SP_TOOL;
        return *e ? e + 7 : e;
    }
    if (!strncmp(s, "<res>", 5)) {
        const char *e = strstr(s, "</res>");
        if (!e) e = s + strlen(s);
        int n = (int)(e - s - 5); if (n >= cap) n = cap - 1;
        memcpy(out, s + 5, (size_t)n); out[n] = 0;
        *k = SP_RES;
        return *e ? e + 6 : e;
    }
    const char *e = s;
    for (;;) {                                   /* a bare '<' does not end the run; a tag does */
        while (*e && *e != '<') e++;
        if (!*e || istag(e)) break;
        e++;
    }
    int n = (int)(e - s); if (n >= cap) n = cap - 1;
    memcpy(out, s, (size_t)n); out[n] = 0;
    *k = SP_TEXT;
    if (!*e) return e;
    if (!strncmp(e, "<a>", 3)) return e + 3;
    if (!strncmp(e, "<end>", 5)) return e + 5;
    if (!strncmp(e, "<tool>", 6) || !strncmp(e, "<res>", 5)) return e;   /* handled on re-entry */
    /* An UNRECOGNISED tag is skipped WHOLE, not one character at a time. Advancing past just the
     * '<' left the tag name and its body as literal text, so a stray "<r>12</r>" rendered on screen
     * as "r>12 ... /r>". The model emits malformed markup often enough that this is the common
     * case -- degrade to dropping the tag, never to showing its guts. */
    {   const char *gt = e;
        while (*gt && *gt != '>') gt++;
        if (*gt == '>') return gt + 1;
    }
    return e + 1;
}

/* "eval<arg> (150.0)/(12.0)" -> fn "eval", arg "(150.0)/(12.0)" */
/* Unused by the device build since the chips were hidden, and KEPT rather than deleted: this is the
 * tool-call parser, and linking the evaluator in needs it back. It is not dead in the sense that
 * matters -- tools/eval/test_span.c exercises all four of its cases, including the multi-argument
 * separator. Deleting it would take working, tested code out with a UI decision. */
__attribute__((unused))
static void split_call(const char *in, char *fn, int fcap, char *arg, int acap) {
    const char *a = strstr(in, "<arg>");
    /* no <arg>: the whole span is the function name. Explicit precision -- a malformed span can
     * be the entire remaining generation, and fn is 24 bytes. */
    if (!a) { snprintf(fn, (size_t)fcap, "%.*s", fcap - 1, in); arg[0] = 0; return; }
    int n = (int)(a - in); if (n >= fcap) n = fcap - 1;
    while (n && in[n - 1] == ' ') n--;
    memcpy(fn, in, (size_t)n); fn[n] = 0;
    const char *p = a + 5; while (*p == ' ') p++;
    /* explicit precision: the argument is drawn in a chip and a runaway span must be cut here,
     * visibly, rather than silently filling the buffer */
    snprintf(arg, (size_t)acap, "%.*s", acap - 1, p);
    /* <arg> is a SEPARATOR, not a wrapper -- the spec is <tool>NAME<arg>A1<arg>A2</tool>. Taking the
     * tail whole leaked a literal "<arg>" into the chip on every multi-argument call, and `solve` in
     * the spec's own example takes two. Render the separators as commas. */
    for (char *q = strstr(arg, "<arg>"); q; q = strstr(q, "<arg>")) {
        q[0] = ','; q[1] = ' ';
        memmove(q + 2, q + 5, strlen(q + 5) + 1);
        q += 2;
    }
}

/* ---- drawing --------------------------------------------------------------------------------- */
/* Likewise unused since the call and result chips were hidden. Kept because it is the only
 * implementation of that visual and the decision to hide them is a preference, not a defect. */
__attribute__((unused))
static int chip(int x, int y, const char *label, const char *val, uint16_t bg, uint16_t ln,
                uint16_t fg, int draw) {
    int lw = label ? gfx_text_w(label, F_UIB) : 0;
    int gap = (label && val) ? 4 : 0;
    int vw = val ? gfx_text_w(val, F_UI) : 0;
    int w = 10 + lw + gap + vw;
    int h = gfx_font_h(F_UI) + 4;
    if (draw) {
        gfx_rrect(x, y, w, h, 4, bg);
        gfx_rrect_outline(x, y, w, h, 4, ln);
        int tx = x + 5;
        if (label) tx += gfx_text(tx, y + 2, label, F_UIB, fg, bg) + gap;
        if (val) gfx_text(tx, y + 2, val, F_UI, fg, bg);
    }
    return w;
}

/* Draw one assistant answer; returns the height it occupies. draw=0 measures only. */
/* Draw ONLY what the model says, never how it got there.
 *
 * The tool call and the result used to render as chips. They were the visible evidence that the
 * number came from the evaluator rather than from the model -- but the reader asked for the answer,
 * not the working, and a calculator screen is 320px wide. The spans are still PARSED (span_next
 * consumes them) and still stored verbatim in app_turn.a, so the provenance data is intact for the
 * write-up; it is only not drawn. */
/* ---- notation ----------------------------------------------------------------------------------
 * The corpus writes formulas in a parser-first ASCII: Delta_p=m*Delta_v, omega=sqrt((k)/(m)),
 * theta, lambda, _0, ^2. That is correct for the evaluator and wrong for a reader, and the font
 * has carried the real glyphs the whole time -- all three faces have the Greek lowercase set, both
 * digit runs of sub- and superscripts, and √ ∫ ∂ ≈ ≤ ≥.
 *
 * EVERY TARGET IS CHECKED TO EXIST. gfx_text draws nothing at all for a missing glyph -- never a
 * box -- so mapping to a code point the font lacks does not look wrong, it makes the character
 * silently disappear. tools/eval/test_notation.c asserts every replacement below is present in all
 * three faces, which is the only reason this is safe to do at all.
 *
 * Names are matched on WORD BOUNDARIES, so "pi" in "spin" and "eta" inside "theta" are left alone.
 * Longest first, for the same reason. */
typedef struct { const char *from; const char *to; } sym;
static const sym SYMS[] = {
    /* Greek, longest first so a shorter name cannot claim a prefix */
    {"epsilon","\xce\xb5"}, {"lambda","\xce\xbb"}, {"omega","\xcf\x89"}, {"sigma","\xcf\x83"},
    {"theta","\xce\xb8"},   {"alpha","\xce\xb1"},  {"gamma","\xce\xb3"}, {"delta","\xce\xb4"},
    {"Delta","\xce\x94"},   {"Omega","\xce\xa9"},  {"Sigma","\xce\xa3"},
    {"beta","\xce\xb2"},    {"phi","\xcf\x86"},    {"psi","\xcf\x88"},
    {"chi","\xcf\x87"},     {"tau","\xcf\x84"},    {"rho","\xcf\x81"},   {"eta","\xce\xb7"},
    {"mu","\xce\xbc"},      {"nu","\xce\xbd"},     {"xi","\xce\xbe"},    {"pi","\xcf\x80"},
    /* operators and relations */
    {"sqrt","\xe2\x88\x9a"}, {"<=","\xe2\x89\xa4"}, {">=","\xe2\x89\xa5"},
    {"!=","\xe2\x89\x88"},
};
static const char *SUB[10] = {"\xe2\x82\x80","\xe2\x82\x81","\xe2\x82\x82","\xe2\x82\x83",
                              "\xe2\x82\x84","\xe2\x82\x85","\xe2\x82\x86","\xe2\x82\x87",
                              "\xe2\x82\x88","\xe2\x82\x89"};
static const char *SUP[10] = {"\xe2\x81\xb0","\xc2\xb9","\xc2\xb2","\xc2\xb3",
                              "\xe2\x81\xb4","\xe2\x81\xb5","\xe2\x81\xb6","\xe2\x81\xb7",
                              "\xe2\x81\xb8","\xe2\x81\xb9"};

static int wordch2(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
/* Rewrite `in` for DISPLAY. Never changes what is stored or what the evaluator sees. */
void to_display(const char *in, char *out, int cap) {
    int o = 0;
    for (int i = 0; in[i] && o < cap - 4; ) {
        /* _0.._9 and ^0..^9 become real sub/superscripts; a letter after _ is left alone, because
         * the font has no subscript letters and dropping the underscore would fuse "f_beat". */
        if ((in[i] == '_' || in[i] == '^') && in[i+1] >= '0' && in[i+1] <= '9') {
            const char *g = (in[i] == '_' ? SUB : SUP)[in[i+1] - '0'];
            for (int k = 0; g[k] && o < cap - 1; k++) out[o++] = g[k];
            i += 2; continue;
        }
        int hit = 0;
        for (unsigned s = 0; s < sizeof SYMS / sizeof SYMS[0]; s++) {
            int L = (int)strlen(SYMS[s].from);
            if (strncmp(in + i, SYMS[s].from, (size_t)L) != 0) continue;
            /* word boundary, so "pi" in "spin" and "eta" in "theta" are not touched. Operators are
             * not alphabetic and need no boundary. */
            if (wordch2(SYMS[s].from[0])) {
                if (i > 0 && wordch2(in[i-1])) continue;
                if (wordch2(in[i+L])) continue;
            }
            for (int k = 0; SYMS[s].to[k] && o < cap - 1; k++) out[o++] = SYMS[s].to[k];
            i += L; hit = 1; break;
        }
        if (!hit) out[o++] = in[i++];
    }
    out[o] = 0;
}

/* One status line: a dot, a label, and an optional detail in the lighter ink. Grey and a step down
 * from the answer, so a reader never has to work out which line is the model talking.
 *
 * Returns the height used. Drawing and measuring go through the same call with `draw` 0 or 1, which
 * is how the question bubble's measure/draw split went wrong -- one function, two modes, no chance
 * for the two to disagree. */
static int draw_status(int x, int y, int w, const char *label, const char *mono, int live, int draw) {
    int lh = gfx_font_h(F_UI);
    if (!label || !label[0]) return 0;
    if (draw) {
        /* the dot: filled while working, hollow once finished */
        if (live) gfx_rrect(x, y + lh / 2 - 2, 5, 5, 2, C_INK3);
        else      gfx_rrect_outline(x, y + lh / 2 - 2, 5, 5, 2, C_INK3);
        int tx = x + 10;
        tx += gfx_text(tx, y, label, F_UI, C_INK3, C_BG);
        if (mono && mono[0]) {
            tx += gfx_text(tx, y, " ", F_UI, C_INK3, C_BG);
            gfx_text_ellipsis(tx, y, mono, F_UI, C_INK2, C_BG, x + w - tx);
        }
    }
    return lh + 4;
}

static int draw_answer(int x, int y, int w, const char *raw, int draw) {
    int lh = gfx_font_h(F_UI) + 2, cy = y;
    const char *p = raw;
    char buf[512]; sp_kind k;
    while ((p = span_next(p, &k, buf, sizeof buf)) != 0) {
        if (k == SP_TEXT) {
            const char *s = buf; while (*s == ' ') s++;
            if (*s) {
                static char disp[640];
                to_display(s, disp, sizeof disp);
                int n = gfx_text_wrap(x, cy, disp, F_UI, C_INK, C_BG, w, lh, draw);
                cy += n * lh;
            }
        }
        if (!*p) break;
    }
    return cy - y;
}


/* ---- search ------------------------------------------------------------------------------------
 * The same CONTENT search the web UI runs, ported rather than reinvented: every term must appear
 * somewhere in the session -- title OR any message -- and match KIND outranks position, so a whole
 * word beats a word-start beats a mid-word substring. Scoring the title alone was the defect on the
 * web side and it would have shipped here too.
 *
 * There is no dynamic allocation: the model owns the heap, so scoring lowercases one string at a
 * time into a fixed scratch rather than building a per-session haystack. */
static int  SEARCH_ON;
static char SQ[40];
static int  SQ_N;
static int  SHIT[MAX_CHATS], NSHIT, SSEL;
static int  SSCROLL;                 /* first hit row drawn in the sheet */
#define SHEET_ROWS 5                 /* what fits in a 320x240 sheet without covering the composer */
static gfx_rect R_SEARCH, R_SROW[MAX_CHATS];

#define MAX_TERMS 5
#define TERM_MAX  20

static char LOWBUF[544];
static const char *lowr(const char *s) {
    int i = 0;
    for (; s[i] && i < (int)sizeof LOWBUF - 1; i++) {
        char c = s[i];
        LOWBUF[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    LOWBUF[i] = 0;
    return LOWBUF;
}
static int wordch(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

/* Best score for one term in one lowercased haystack, or -1 when absent. */
static int term_score(const char *hay, const char *term, int base) {
    int best = -1, tl = (int)strlen(term), hl = (int)strlen(hay);
    if (!tl) return -1;
    for (const char *p = strstr(hay, term); p; p = strstr(p + 1, term)) {
        int i = (int)(p - hay);
        int pre  = (i == 0) || !wordch(hay[i - 1]);
        int post = (i + tl >= hl) || !wordch(hay[i + tl]);
        int kind = (pre && post) ? 300 : pre ? 150 : 0;
        int s = base + kind - (i > 200 ? 200 : i) / 10;
        if (s > best) best = s;
    }
    return best;
}
static int split_terms(const char *q, char out[MAX_TERMS][TERM_MAX]) {
    int n = 0, k = 0;
    for (const char *p = q; ; p++) {
        if (*p && *p != ' ') {
            if (k < TERM_MAX - 1) {
                char c = *p;
                out[n][k++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
            }
        } else if (k) {
            out[n][k] = 0; k = 0;
            if (++n >= MAX_TERMS) break;
        }
        if (!*p) break;
    }
    return n;
}
static int chat_score(const app_chat *c, char terms[MAX_TERMS][TERM_MAX], int nt) {
    long total = 0;
    for (int i = 0; i < nt; i++) {
        int s = term_score(lowr(c->title), terms[i], 1000);
        for (int j = 0; j < c->nturns; j++) {
            int v = term_score(lowr(c->turn[j].q), terms[i], 500); if (v > s) s = v;
            v     = term_score(lowr(c->turn[j].a), terms[i], 500); if (v > s) s = v;
        }
        if (s < 0) return -1;                  /* AND: a term nobody has disqualifies the session */
        total += s;
    }
    return nt ? (int)(total / nt) : 0;
}
/* Rebuild the ranked hit list. Insertion sort: MAX_CHATS is 12. */
static void run_search(void) {
    char terms[MAX_TERMS][TERM_MAX];
    int nt = split_terms(SQ, terms);
    int score[MAX_CHATS];
    NSHIT = 0;
    for (int i = 0; i < NCHATS; i++) {
        int s = nt ? chat_score(&CHATS[i], terms, nt) : 0;
        if (s < 0) continue;
        int at = NSHIT;
        while (at > 0 && score[at - 1] < s) { score[at] = score[at - 1]; SHIT[at] = SHIT[at - 1]; at--; }
        score[at] = s; SHIT[at] = i; NSHIT++;
    }
    if (SSEL >= NSHIT) SSEL = 0;
    SSCROLL = 0;
}
/* Window around the earliest match in the earliest message that has one. */
static void snippet_of(const app_chat *c, char terms[MAX_TERMS][TERM_MAX], int nt,
                       char *out, int cap) {
    out[0] = 0;
    for (int j = 0; j < c->nturns; j++) {
        for (int which = 0; which < 2; which++) {
            const char *src = which ? c->turn[j].a : c->turn[j].q;
            if (!src[0]) continue;
            const char *low = lowr(src);
            int at = -1;
            for (int i = 0; i < nt; i++) {
                const char *p = strstr(low, terms[i]);
                if (p) { int k = (int)(p - low); if (at < 0 || k < at) at = k; }
            }
            if (at < 0) continue;
            int from = at - 24; if (from < 0) from = 0;
            /* snap forward to a word boundary: a window opening mid-number rendered ".3 m/s" and
             * read as a decimal point rather than as elision */
            if (from > 0) { while (src[from] && src[from] != ' ') from++; while (src[from] == ' ') from++; }
            int n = 0;
            if (from > 0) { for (int e = 0; e < 3 && n < cap - 1; e++) out[n++] = '.'; out[n++] = ' '; }
            for (int i = from; src[i] && i < from + 74 && n < cap - 1; i++) out[n++] = src[i];
            out[n] = 0;
            return;
        }
    }
    if (c->nturns) { strncpy(out, c->turn[0].q, cap - 1); out[cap - 1] = 0; }
}

/* A 9px lens. gfx_rrect_outline at r=4 degenerates to a diamond -- its corner test is a radius
 * compare per row, which is right at r>=6 and visibly wrong below it. */
/* Defined further down, beside the cursor it was written for. */
static void blit(int x, int y, const char *const *rows, int n, uint16_t ink, uint16_t bg);

/* The lens, written out. Computing it from i*i + j*j gave a ring whose thickness varied around
 * the circle -- thin on the diagonals, doubled on the axes -- which at this size reads as notches,
 * or as the user put it, a gear. R is kept in the signature because the search sheet draws a
 * smaller one. */
static const char *LENS11[] = {
    "   ####   ",
    " ##    ## ",
    " #      # ",
    "#        #",
    "#        #",
    "#        #",
    "#        #",
    " #      # ",
    " ##    ## ",
    "   ####   ",
};
static const char *LENS8[] = {
    "  ####  ",
    " #    # ",
    "#      #",
    "#      #",
    "#      #",
    "#      #",
    " #    # ",
    "  ####  ",
};

static void magnifier(int x, int y, int R, uint16_t c) {
    int big = R >= 5;
    const char *const *rows = big ? LENS11 : LENS8;
    int n = big ? 10 : 8;
    blit(x, y, rows, n, c, c);              /* ink only: no fill in a lens */
    for (int k = 0; k < (big ? 6 : 4); k++) /* the handle, off the lower right */
        gfx_fill(x + n - 1 + k, y + n - 1 + k, 2, 1, c);
}

/* compose glyph for New chat: the web row has one and the device row did not, so the two rows sat
 * on different text baselines. */
/* The "new chat" mark, drawn to read as the web's edit icon rather than as a stray diagonal.
 *
 * It used to be a 7px shaft with two dots, which at 11px reads as "/" -- fine beside the word
 * "New chat" and meaningless once the label went away and it had to carry the button alone. This
 * is the same figure the SVG draws: a page open at its top-right corner, with the pencil crossing
 * the gap. 13x13 from (x,y). */
/* The panel-toggle and exit marks, as functions because each is drawn from TWO places now and a
 * second hand-inlined copy is how they diverge. Both are sized to the same 20x20 plate and the
 * same 1px weight as the magnifier and the pencil, so every control in the app is one class. */
/* ALL THREE GLYPHS ARE 16x16 ON THE SAME ORIGIN, x+4 y+4 inside a 24x24 plate, and all are 1px.
 * Doubling the strokes made them heavy rather than larger, and each grew by a different amount so
 * the three stopped matching each other. Size comes from the geometry now; weight does not. */
static void panel_icon(int x, int y, uint16_t c) {
    /* radius 2, not 3: at 16x14 the larger radius leaves visible gaps at each corner, so the box
     * reads as broken rather than rounded. */
    gfx_rrect_outline(x + 4, y + 5, 16, 14, 2, c);
    gfx_vline(x + 10, y + 5, 14, c);
}
static void exit_icon(int x, int y, uint16_t c) {    /* an X on the same 13x12 optical box */
    for (int i = 0; i < 9; i++) {
        gfx_fill(x + 5 + i, y + 5 + i, 1, 1, c);
        gfx_fill(x + 5 + i, y + 13 - i, 1, 1, c);
    }
}

/* NEW CHAT IS A PLUS, not a pencil.
 *
 * A pencil-in-a-page needs a page, a gap, a shaft and a tip, and at 16px on this panel the page
 * fades and the shaft dominates, so it reads as a bare diagonal line -- which is what it looked
 * like on the hardware. It also carried more ink high in the cell than the other two glyphs, which
 * is why it sat visibly higher on a row that is otherwise level. A plus means "new" without
 * ambiguity, is symmetric so it cannot look elevated, and is exact at any size.
 *
 * 2px strokes because this is a solid mark rather than an outline: the same optical weight as the
 * 1px outlines beside it. */
static void plus_icon(int x, int y, uint16_t c) {
    /* 1px, matching the outlines beside it. At 2px this was a solid mark next to two hairline
     * outlines and read as the heaviest thing on the row -- the reasoning that it needed doubling
     * to carry the same optical weight was simply wrong on this panel. */
    gfx_fill(x + 4,  y + 11, 16, 1, c);      /* the bar, centred in a 16x16 cell */
    gfx_fill(x + 11, y + 4,  1, 16, c);
}

static void pencil(int x, int y, uint16_t c) {
    /* A page open at its top-right corner, the pencil crossing the gap. 16x16, 1px. */
    /* The page: 13 wide, 12 tall, open at the top-right. Its bottom edge was 15 wide against a
     * 13-wide box, so the shape ran past its own corner and looked oversized and crooked. */
    gfx_hline(x + 1,  y + 6,  7, c);       /* top, stopping short of the corner */
    gfx_vline(x + 1,  y + 6, 12, c);       /* left */
    gfx_hline(x + 1,  y + 17, 13, c);      /* bottom */
    gfx_vline(x + 13, y + 11, 7, c);       /* right, resuming below the gap */
    /* The pencil crosses the open corner and STOPS inside the 16px box. */
    for (int k = 0; k < 7; k++)
        gfx_fill(x + 7 + k, y + 11 - k, 2, 1, c);
    gfx_fill(x + 6, y + 12, 2, 2, c);      /* its tip */
}

/* Draw `s` with the parts matching any term in the BOLD face. The device had no equivalent of the
 * web's <b> wrapping, so a result gave no indication of WHY it matched. Two weights is all the font
 * has, so the title stays bold throughout and only the snippet carries the emphasis. */
static int draw_marked(int x, int y, const char *s, char terms[MAX_TERMS][TERM_MAX], int nt,
                       uint16_t fg, uint16_t bg, int maxw) {
    char low[128];
    unsigned char mark[128];
    int n = 0;
    for (; s[n] && n < (int)sizeof low - 1; n++) {
        char c = s[n];
        low[n] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    low[n] = 0;
    memset(mark, 0, sizeof mark);
    for (int i = 0; i < nt; i++) {
        int tl = (int)strlen(terms[i]);
        if (!tl) continue;
        for (const char *p = strstr(low, terms[i]); p; p = strstr(p + 1, terms[i])) {
            int at = (int)(p - low);
            for (int k = at; k < at + tl && k < n; k++) mark[k] = 1;
        }
    }
    int x0 = x, ew = gfx_text_w("...", F_UI);
    for (int i = 0; i < n; i++) {
        char one[2]; one[0] = s[i]; one[1] = 0;
        gfx_font f = mark[i] ? F_UIB : F_UI;
        int w = gfx_text_w(one, f);
        if (x - x0 + w + (s[i + 1] ? ew : 0) > maxw) { gfx_text(x, y, "...", F_UI, fg, bg); break; }
        x += gfx_text(x, y, one, f, fg, bg);
    }
    return x - x0;
}

static void draw_search(void) {
    gfx_dim(C_SCRIM, 28);                  /* same scrim weight as the web popup */

    const int SW = 260, SX = (GFX_W - SW) / 2, SY = 26;
    /* The sheet SCROLLS. It used to cap at five hits and stop, so a sixth match was ranked, counted
     * and unreachable -- the same hide-rather-than-truncate defect the session list had. */
    if (SSEL < SSCROLL) SSCROLL = SSEL;
    if (SSEL >= SSCROLL + SHEET_ROWS) SSCROLL = SSEL - SHEET_ROWS + 1;
    if (SSCROLL > NSHIT - SHEET_ROWS) SSCROLL = NSHIT - SHEET_ROWS;
    if (SSCROLL < 0) SSCROLL = 0;
    int rows = NSHIT - SSCROLL;
    if (rows > SHEET_ROWS) rows = SHEET_ROWS;
    int SH = 30 + (rows ? rows * 30 + 6 : 26) + (NSHIT > SHEET_ROWS ? 14 : 0);
    gfx_rrect(SX - 1, SY - 1, SW + 2, SH + 2, 9, C_LINE);
    gfx_rrect(SX, SY, SW, SH, 8, C_SHEET);

    /* field */
    gfx_hline(SX + 10, SY + 27, SW - 20, C_LINE);
    magnifier(SX + 11, SY + 8, 4, C_INK3);
    if (SQ_N) gfx_text(SX + 28, SY + 7, SQ, F_UI, C_INK, C_SHEET);
    else      gfx_text(SX + 28, SY + 7, "Search chats...", F_UI, C_INK3, C_SHEET);
    if (SQ_N) gfx_vline(SX + 29 + gfx_text_w(SQ, F_UI), SY + 8, 12, C_INK);

    char terms[MAX_TERMS][TERM_MAX];
    int nt = split_terms(SQ, terms);

    if (!rows) {
        gfx_text(SX + 12, SY + 36, SQ_N ? "No chats match" : "No chats yet", F_UI, C_INK3, C_SHEET);
        return;
    }
    for (int i = 0; i < rows; i++) {
        int y = SY + 34 + i * 30;
        R_SROW[i] = (gfx_rect){ SX + 4, y - 2, SW - 8, 28 };
        int hit = SSCROLL + i;
        int hot = (hit == SSEL) || (HOVER && inside(R_SROW[i], MX, MY));
        uint16_t bg = hot ? C_SEL : C_SHEET;
        if (hot) gfx_rrect(R_SROW[i].x, R_SROW[i].y, R_SROW[i].w, R_SROW[i].h, 5, bg);
        const app_chat *c = &CHATS[SHIT[hit]];
        gfx_text_ellipsis(SX + 10, y, c->title, F_UIB, C_INK, bg, SW - 20);
        if (nt) {
            char sn[110];
            snippet_of(c, terms, nt, sn, sizeof sn);
            draw_marked(SX + 10, y + 13, sn, terms, nt, C_INK2, bg, SW - 20);
        }
    }
    /* Say how many are off the sheet, rather than letting them be silently absent. */
    if (NSHIT > SHEET_ROWS) {
        char more[40];
        snprintf(more, sizeof more, "%d of %d  -  arrows for more", SSCROLL + rows, NSHIT);
        gfx_text(SX + 10, SY + 34 + rows * 30 - 1, more, F_UI, C_INK3, C_SHEET);
    }
}

static void draw_sidebar(void) {
    gfx_fill(0, 0, SIDE_W, GFX_H, C_SIDE);
    gfx_vline(SIDE_W, 0, GFX_H, C_LINE);

    /* The icon band. THREE controls, 20x20 on a 22px pitch: new chat at the left end, search
     * and the panel toggle at the right -- the desktop's grouping, primary action apart from tools.
     *
     * There is no theme control. It cycled auto -> light -> dark and spent a quarter of the band
     * on a choice the machine can make itself: TH_AUTO reads the clock. Removing it is what buys
     * the remaining three the room to go from 18px to 20px, which at arm's length is the
     * difference between a glyph that reads and one that does not. */
    /* EVENLY distributed: 3 * 20 + 4 * 5 = 80 = SIDE_W, so the gap between any two icons and the
     * gap at either end are all 5px. The previous layout pinned one icon left and two right, which
     * left 19px on one side of the pair and 2px on the other -- not a grouping, just lopsided. */
    R_NEW    = (gfx_rect){ 4,  3, 24, 24 };
    R_SEARCH = (gfx_rect){ 32, 3, 24, 24 };
    R_TOGGLE = (gfx_rect){ 60, 3, 24, 24 };

    /* Hover darkens the plate AND the glyph. The plate alone is a very small cue at 20px on a
     * panel with this contrast; the ink moving from C_INK2 to C_INK is what actually reads. */
    {   int hot = HOVER && hit(R_NEW, MX, MY);
        gfx_rrect(R_NEW.x, R_NEW.y, 24, 24, 6, hot ? C_SEL : C_SIDE);
        plus_icon(R_NEW.x, R_NEW.y, hot ? C_INK : C_INK2);
    }
    {   int sh = HOVER && hit(R_SEARCH, MX, MY);
        gfx_rrect(R_SEARCH.x, R_SEARCH.y, 24, 24, 6, sh ? C_SEL : C_SIDE);
        magnifier(R_SEARCH.x + 4, R_SEARCH.y + 4, 6, sh ? C_INK : C_INK2);
    }
    {   int th = HOVER && hit(R_TOGGLE, MX, MY);
        gfx_rrect(R_TOGGLE.x, R_TOGGLE.y, 24, 24, 6, th ? C_SEL : C_SIDE);
        panel_icon(R_TOGGLE.x, R_TOGGLE.y, th ? C_INK : C_INK2);
    }

    /* A heading over nothing is furniture. */
    if (NCHATS) gfx_text(9, TOP_H + 6, "Recents", F_SM, C_INK3, C_SIDE);

    /* The list SCROLLS. It used to break at the first row that did not fit, which silently hid up
     * to six of a twelve-session cap -- unreachable, with nothing on screen admitting it. Hiding
     * data is worse than truncating it, because truncation is visible. */
    {
        /* +20, not +18: "Recents" is drawn at TOP_H+2 and F_UI's box is 15 tall, so it occupies rows
         * 26..40. R_CHAT[0] starts at top-2, so top must be at least 43 for the first row's
         * highlight not to sit on the heading's last row. 44 keeps CHAT_FIT at 8. */
        /* F_SM's box is 13, so a row is 14 on a 15px pitch instead of 17-on-18. Between that and
         * the reclaimed footer band, CHAT_FIT reaches 12 -- the whole MAX_CHATS cap, visible at
         * once, so the scrollbar only ever appears if that cap changes. */
        /* +26 puts the first row 4px below the "Recents" box (TOP_H+8, 13 tall -> ends at
         * TOP_H+21) instead of hard against it. The 15px pitch still lands CHAT_FIT on 12. */
        /* +24 with TOP_H at 30. "Recents" occupies TOP_H+6 .. TOP_H+18 and R_CHAT[0] starts at
         * top-2, so this leaves 4px of air under the heading and still lands CHAT_FIT on 12 --
         * the taller icon band cost a row and this is where it comes back. */
        int top = TOP_H + 24, bot = GFX_H - 6;
        CHAT_FIT = (bot - top) / 15;
        if (CHAT_FIT < 1) CHAT_FIT = 1;
        R_LIST = (gfx_rect){ 0, top - 4, SIDE_W, bot - top + 8 };

        int maxs = NCHATS - CHAT_FIT; if (maxs < 0) maxs = 0;
        if (CHAT_SCROLL > maxs) CHAT_SCROLL = maxs;
        if (CHAT_SCROLL < 0) CHAT_SCROLL = 0;

        int over = NCHATS > CHAT_FIT;
        int rowmax = over ? SIDE_W - 14 : SIDE_W - 8;   /* leave room for the bar when it shows */

        /* One tick per draw. Reset when nothing is hovered so the next hover starts from the
         * left rather than resuming mid-scroll. */
        int any_hot = 0;

        NCHAT_ROWS = 0;
        for (int r = 0; r < CHAT_FIT && CHAT_SCROLL + r < NCHATS; r++) {
            int i = CHAT_SCROLL + r, y = top + r * 15;
            CHAT_AT[r] = i;
            R_CHAT[r]  = (gfx_rect){ 4, y - 2, rowmax, 14 };
            R_TRASH[r] = (gfx_rect){ rowmax - 13, y - 1, 13, 13 };
            int hot = HOVER && inside(R_CHAT[r], MX, MY);
            if (hot) any_hot = 1;
            /* Selected by the arrows, or open, or under the finger: all three look the same,
             * because they mean the same thing to the reader. A selection the keys move but the
             * screen does not show is the unwired-affordance bug again. */
            int sel = (CUR < 0 && i == SEL_ROW);
            uint16_t bg = (i == CUR || hot || sel) ? C_SEL : C_SIDE;
            if (i == CUR || hot || sel) gfx_rrect(R_CHAT[r].x, R_CHAT[r].y, R_CHAT[r].w, R_CHAT[r].h, 4, bg);
            {   int avail = rowmax - (hot ? 26 : 10);   /* the trash takes room only while hovered */
                int tw = gfx_text_w(CHATS[i].title, F_SM);
                if (hot && tw > avail) {
                    if (MARQ_AT != i) { MARQ_AT = i; MARQ_T0 = NOW_MS; }
                    int off = marq_off(marq_elapsed(), tw - avail);
                    /* Clipped to the title band, so the scrolled tail cannot run under the trash
                     * or out of the sidebar. */
                    gfx_clip(9, y, avail, gfx_font_h(F_SM));
                    gfx_text(9 - off, y, CHATS[i].title, F_SM, C_INK, bg);
                    gfx_clip_reset();
                } else {
                    gfx_text_ellipsis(9, y, CHATS[i].title, F_SM, C_INK, bg, avail);
                }
            }
            if (hot) {                    /* trash appears only on hover, as on the web */
                int tx = R_TRASH[r].x, ty = R_TRASH[r].y;
                uint16_t tb = inside(R_TRASH[r], MX, MY) ? C_TRASH_HOT : bg;
                gfx_rrect(tx, ty, 14, 14, 3, tb);
                gfx_hline(tx + 3, ty + 4, 9, C_INK2);      /* lid */
                gfx_hline(tx + 6, ty + 2, 3, C_INK2);      /* handle */
                gfx_vline(tx + 4, ty + 5, 7, C_INK2);      /* body sides */
                gfx_vline(tx + 10, ty + 5, 7, C_INK2);
                gfx_hline(tx + 4, ty + 11, 7, C_INK2);     /* base */
                gfx_vline(tx + 7, ty + 6, 5, C_INK2);      /* tine */
            }
            NCHAT_ROWS = r + 1;
        }
        if (!any_hot) MARQ_AT = -1;

        /* A scrollbar, so "there is more" is visible rather than inferred. */
        if (over) {
            int track_h = CHAT_FIT * 15, tx = SIDE_W - 7;
            int knob = track_h * CHAT_FIT / NCHATS; if (knob < 12) knob = 12;
            int ky = top - 2 + (track_h - knob) * CHAT_SCROLL / maxs;
            gfx_rrect(tx, top - 2, 3, track_h, 1, C_BAR);
            gfx_rrect(tx, ky, 3, knob, 1, C_INK3);
        }
    }

    /* No sidebar footer. It spent the whole DOCK_H band -- a sixth of the column -- restating two
     * numbers that do not change while the app runs and that the poster and the README both carry.
     * The list gets the band instead: CHAT_FIT goes from 8 rows to 10, which is most of the
     * MAX_CHATS cap visible without scrolling. */
}


/* Text width available inside a question bubble, for a given pane width.
 *
 * ONE definition, because there were two: the measure pass used pw-26 and the draw pass pw-32, so
 * the bubble was sized for fewer lines than were actually drawn and a long question spilled out
 * through the bottom of its own bubble. The bubble sits at px0+16 with 8px of padding each side,
 * which is where 32 comes from -- the 26 was simply wrong. */
static int qbubble_textw(int pane_w) { return pane_w - 32; }

static void draw_main(void) {
    int x0 = SIDEBAR ? SIDE_W + 1 : 0;
    int w  = GFX_W - x0;
    gfx_fill(x0, 0, w, GFX_H, C_BG);

    /* With the sidebar closed there was NOTHING on screen to reopen it. R_TOGGLE kept whatever
     * rect it held when the sidebar last drew, so the control stayed clickable while being
     * invisible -- an affordance that exists only in the source, which is the thing the exit
     * button's own comment warns about. Closed, the toggle moves into the top bar.
     *
     * The title starts after it, and both are centred in TOP_H: the icon plate is 20 in a 26 band
     * (y=3) and the F_BIG box is 18 (y=4), so the two share a centre line at 13. */
    int tx = x0 + PAD;
    if (!SIDEBAR) {
        R_TOGGLE = (gfx_rect){ 4, 3, 24, 24 };
        int th = HOVER && hit(R_TOGGLE, MX, MY);
        gfx_rrect(R_TOGGLE.x, R_TOGGLE.y, 24, 24, 6, th ? C_SEL : C_BG);
        panel_icon(R_TOGGLE.x, R_TOGGLE.y, C_INK2);
        tx = R_TOGGLE.x + 24 + 6;
    }

    /* The title is the CHAT's title, and on the new-chat screen there is no chat. "ChatTLM" was
     * being stated three times on one screen -- top bar, "Ask ChatTLM" in the field, and
     * "ChatTLM can make mistakes" under it -- while the heading already identifies the app. */
    if (CUR >= 0)
        /* F_UIB, not F_BIG. The title is bold 13 now: an 18px box on a 26px bar left the icons
         * boxed in beside it, and the string is the chat's own opening words, which the transcript
         * repeats immediately underneath. Bold keeps it the heading of the pane without it being
         * the largest thing on screen. Box 15 in a 26px band centres at y=5. */
        gfx_text_ellipsis(tx, 7, CHATS[CUR].title, F_UIB, C_INK, C_BG, GFX_W - 30 - tx);

    /* EXIT, top right, always drawn. ESC has always quit, but nothing on screen said so, and a
     * judge handed the calculator does not know the key. An affordance that exists only in the
     * source is not an affordance. */
    /* Same 20x20 plate, same corner radius and same weight as the sidebar's three. It was an 18px
     * box with a 2px-thick hand-drawn X, which made it visibly heavier and smaller than every
     * other control on screen. */
    R_EXIT = (gfx_rect){ GFX_W - 28, 3, 24, 24 };
    {   int hot = HOVER && hit(R_EXIT, MX, MY);
        gfx_rrect(R_EXIT.x, R_EXIT.y, 24, 24, 6, hot ? C_EXIT_HOT : C_BG);
        exit_icon(R_EXIT.x, R_EXIT.y, hot ? C_ERRFG : C_INK2);
    }
    /* No rule under the title. The desktop build draws none -- #topbar has no border -- and at
     * 231px wide a full-width divider under a 60px word reads as a seam across the pane rather
     * than as structure. The sidebar's vline still separates the two columns, which is the only
     * division that carries meaning here. */

    /* transcript */
    int px0 = x0 + PAD, pw = w - 2 * PAD;
    /* The dock is whatever the composer currently needs, not a constant. DOCK_H is its one-line
     * value and stays the floor; as the field grows the transcript's bottom rises to meet it, so
     * the field's BOTTOM edge never moves and the text it holds grows upward. */
    int dock = compose_dock_h(w);
    if (dock < DOCK_H) dock = DOCK_H;
    int top = TOP_H, bot = GFX_H - dock;
    gfx_clip(x0, top, w, bot - top);

    EMPTY_COMPOSER = 0;
    if (CUR < 0) {
        /* This is a scaled rendering of the web empty state, not a re-layout of it. There, `#empty`
         * is `justify-content:center; align-items:center` with the composer reparented INTO it --
         * so the heading and the field sit together in the middle of the pane and the bottom dock
         * is empty. The device did neither: it pinned the heading top-left and left the composer
         * docked, which is why the two screens read as different products.
         *
         * The suggestion rows are gone. They ellipsised at this width ("A car goes 150 m in 12 s.
         * Find ..."), so the one thing they existed to do -- show what a question looks like -- was
         * exactly what they could not do here. */
        int gap = 14;
        int hh = gfx_font_h(F_BIG);
        int blk = hh + gap + compose_field_h(w);             /* heading + gap + field */
        int cy0 = top + (bot - top - blk) / 2;
        if (cy0 < top + 6) cy0 = top + 6;

        /* Heading then field, nothing between -- `#empty` is `h1` + composer and no subtitle. The
         * "Ask in your own words." line was mine, not the design's. */
        const char *h1 = "What can I work out?";
        gfx_text(x0 + (w - gfx_text_w(h1, F_BIG)) / 2, cy0, h1, F_BIG, C_INK, C_BG);
        draw_composer(x0, w, cy0 + hh + gap);
        EMPTY_COMPOSER = 1;
    } else {
        app_chat *c = &CHATS[CUR];
        int lh0 = gfx_font_h(F_UI) + 2, total = 6;
        for (int i = 0; i < c->nturns; i++) {
            app_turn *t = &c->turn[i];
            total += gfx_text_wrap(0, 0, t->q, F_UI, C_INK, C_BUBBLE, qbubble_textw(pw), lh0, 0) * lh0 + 8 + 8;
            if (BUSY && i == c->nturns - 1 && STATUS[0])
                total += draw_status(0, 0, pw, STATUS, STATUS_MONO, 1, 0);
            else if (t->done && t->sum[0])
                total += draw_status(0, 0, pw, t->sum, 0, 0, 0);
            total += t->a[0] ? draw_answer(0, 0, pw, t->a, 0) + 10 : 20;
        }
        clamp_scroll(total, bot - top);
        int y = top + 6 - SCROLL;
        for (int i = 0; i < c->nturns; i++) {
            app_turn *t = &c->turn[i];
            /* question, right-aligned in a bubble */
            int lh = gfx_font_h(F_UI) + 2;
            int lines = gfx_text_wrap(0, 0, t->q, F_UI, C_INK, C_BUBBLE, qbubble_textw(pw), lh, 0);
            int bh = lines * lh + 8;
            gfx_rrect(px0 + 16, y, pw - 16, bh, 7, C_BUBBLE);
            gfx_text_wrap(px0 + 24, y + 4, t->q, F_UI, C_INK, C_BUBBLE, qbubble_textw(pw), lh, 1);
            y += bh + 8;
            /* The status line sits between the question and the answer: live while this turn is
             * generating, and the permanent one-line summary once it is done. */
            if (BUSY && i == c->nturns - 1 && STATUS[0])
                y += draw_status(px0, y, pw, STATUS, STATUS_MONO, 1, 1);
            else if (t->done && t->sum[0])
                y += draw_status(px0, y, pw, t->sum, 0, 0, 1);

            if (t->a[0]) y += draw_answer(px0, y, pw, t->a, 1) + 10;
            else { gfx_fill(px0, y + 4, 5, 9, C_INK); y += 20; }   /* streaming caret */
        }
    }
    gfx_clip_reset();

    if (!EMPTY_COMPOSER) {
        int cy = GFX_H - dock + 3;
        draw_composer(x0, w, cy);
        /* The web's `.note`, and only under the DOCKED field -- `#empty` has no counterpart, which
         * is why the new-chat screen shows the bar alone. */
        /* F_XS. It is a disclaimer under a text box -- the least important line on the screen --
         * and at F_SM it was the same size as the session titles, which are navigation. */
        const char *note = "Please double-check responses.";
        gfx_text(x0 + (w - gfx_text_w(note, F_XS)) / 2, cy + compose_field_h(w) + 3,
                 note, F_XS, C_INK3, C_BG);
    }

}

/* The pointer.
 *
 * It was a right triangle -- rows widening from 1px to 6px with a vertical left edge -- which is
 * half an arrowhead and reads as a broken shape rather than a cursor. This is the ordinary arrow
 * everyone already knows: a slanted head, a notch, and a tail, drawn as INK on a BG-filled body so
 * it stays legible over the white pane, the grey sidebar and a dark bubble alike.
 *
 * '#' is the outline, '.' the fill, ' ' transparent. Written out rather than computed because the
 * shape is the point, and 16 rows of literal are easier to check by eye than the arithmetic that
 * would generate them.
 */
/* Draw a '#'/'.'/' ' bitmap: ink, background, transparent. Three glyphs use this now.
 *
 * Every shape in this app that was GENERATED by a loop has lost part of itself at least once: the
 * cursor and the send arrow each came out as half an arrow, the magnifier's ring rasterised into
 * notches that read as a gear, and the pencil's box overran its own corner. Written down, a shape
 * can be checked by looking at it. */
static void blit(int x, int y, const char *const *rows, int n, uint16_t ink, uint16_t bg) {
    for (int j = 0; j < n; j++)
        for (int i = 0; rows[j][i]; i++) {
            if (rows[j][i] == ' ') continue;
            gfx_fill(x + i, y + j, 1, 1, rows[j][i] == '#' ? ink : bg);
        }
}

/* The pointer. Slimmer than the 16-row version, which read as a lump.
 *
 * '#' is the outline, '.' the fill, ' ' transparent: an ink edge around a background body, so it
 * stays legible over the white pane, the grey sidebar and a dark bubble alike. Written out rather
 * than generated, because a loop lost half of this shape twice -- once here and once on the send
 * arrow -- and twelve rows of literal can be checked by eye. */
static const char *CURSOR[] = {
    "#",
    "##",
    "#.#",
    "#..#",
    "#...#",
    "#....#",
    "#.....#",
    "#......#",
    "#...####",
    "#..#",
    "#.#",
    "##",
};

static void draw_cursor(void) {
    int rows = (int)(sizeof CURSOR / sizeof CURSOR[0]);
    for (int y = 0; y < rows; y++)
        for (int x = 0; CURSOR[y][x]; x++) {
            if (CURSOR[y][x] == ' ') continue;
            gfx_fill(MX + x, MY + y, 1, 1, CURSOR[y][x] == '#' ? C_INK : C_BG);
        }
}

/* The composer is drawn at a caller-chosen y because it MOVES. On the web build `placeComposer()`
 * reparents the same field between `#centerComposer` and `#bottomComposer`; this is that, and it is
 * the reason the function takes a coordinate instead of reading DOCK_H itself. */
static void draw_composer(int x0, int w, int cy) {
    int fh = compose_field_h(w);
    R_FIELD = (gfx_rect){ x0 + PAD, cy, w - 2 * PAD, fh };
    /* The radius stays a half-height pill at one line and stops growing after that, so a tall
     * field is a rounded rectangle rather than a lozenge. */
    /* A SOLID RING WITH THE MIDDLE PUNCHED OUT, rather than a stroked outline.
     *
     * gfx_rrect_outline walks the curve a pixel at a time, and on a pill the "corner" is nearly the
     * whole end -- radius 12 on a 24px height -- so the steps are far enough apart to leave visible
     * gaps and the edge reads as a dotted line. Filling the border colour and then filling the
     * interior over it gives a continuous 2px edge with no rasterisation seams at all. */
    {   int rad = fh > 24 ? 12 : fh / 2;
        gfx_rrect(R_FIELD.x, R_FIELD.y, R_FIELD.w, R_FIELD.h, rad, C_FIELD_LN);
        gfx_rrect(R_FIELD.x + 2, R_FIELD.y + 2, R_FIELD.w - 4, R_FIELD.h - 4,
                  rad > 2 ? rad - 2 : 1, C_FIELD);
    }

    if (COMPOSE_N) {
        /* Wrapped, and scrolled to the END: with more than COMPOSE_MAX_LINES the earlier lines
         * move off the top so what you are typing stays visible. Clipped to the field, because a
         * scrolled first line would otherwise be drawn above it. */
        int tw = compose_textw(w);
        int total = compose_lines_total(w), shown = compose_lines(w);
        int skip = total - shown, last_w = 0;
        /* Clipped to the TEXT band, not to the field. Clipping to the field left the bottom 5px
         * of the line above the first visible one showing -- a row of glyph-tops with no line
         * under them, which reads as a rendering fault rather than as scrolled text. */
        gfx_clip(R_FIELD.x, cy + 5, R_FIELD.w, shown * COMPOSE_LH);
        gfx_text_wrap_ex(R_FIELD.x + 9, cy + 5 - skip * COMPOSE_LH, COMPOSE, F_UI, C_INK, C_FIELD,
                         tw, COMPOSE_LH, 1, &last_w);
        gfx_clip_reset();
        /* the caret follows the text, on the last visible line */
        int cxx = R_FIELD.x + 9 + last_w + 1, cyy = cy + 6 + (shown - 1) * COMPOSE_LH;
        /* Blinks on the app clock, half a second on and half off. A steady bar reads as a piece
         * of the layout; a blinking one reads as the insertion point. */
        if (last_w < tw - 2 && (NOW_MS / 500u) % 2u == 0u) gfx_vline(cxx, cyy, 12, C_INK);
    } else if (CUR >= 0) {
        /* Inside a session the composer is a reply box, not an invitation. The rotating subject
         * belongs to the new-chat screen, where it is answering "what is this thing for"; carrying
         * it into a conversation would keep advertising the app to someone already using it, and
         * put motion next to the answer they are reading. */
        gfx_text(R_FIELD.x + 9, cy + 5, "Type a message...", F_UI, C_INK3, C_FIELD);
    } else {
        const char *pre = "Ask me about ";
        int px = R_FIELD.x + 9, py = cy + 5, lh = COMPOSE_LH;
        gfx_text(px, py, pre, F_UI, C_INK3, C_FIELD);

        int wx = px + gfx_text_w(pre, F_UI);
        unsigned cycle = ASK_HOLD_MS + ASK_SLIDE_MS;
        unsigned n = NOW_MS / cycle;
        int i = ask_index(n);
        unsigned phase = NOW_MS % cycle;

        /* Clipped to exactly one line, so the word arriving from below and the one leaving above
         * are both cut at the field's text band instead of drawing over the caret row. */
        gfx_clip(wx, py, R_FIELD.w - (wx - R_FIELD.x) - 22, lh);
        if (phase < ASK_HOLD_MS) {
            gfx_text(wx, py, ASK_ABOUT[i], F_UI, C_INK3, C_FIELD);
        } else {
            int d = (int)(((phase - ASK_HOLD_MS) * (unsigned)lh) / ASK_SLIDE_MS);
            gfx_text(wx, py - d,      ASK_ABOUT[i],             F_UI, C_INK3, C_FIELD);
            gfx_text(wx, py + lh - d, ASK_ABOUT[ask_index(n + 1u)], F_UI, C_INK3, C_FIELD);
        }
        gfx_clip_reset();
    }
    /* 14, not 16. In a 20px field a 16px disc leaves 2px of margin and reads as a plug filling
     * the end of the pill rather than as a button sitting inside it. */
    R_SEND = (gfx_rect){ R_FIELD.x + R_FIELD.w - 19, cy + fh - 19, 14, 14 };
    if (BUSY) {
        gfx_rrect(R_SEND.x, R_SEND.y, R_SEND.w, R_SEND.h, 7, C_INK);
        gfx_fill(R_SEND.x + 4, R_SEND.y + 4, 6, 6, C_BG);
    } else {
        uint16_t sb = COMPOSE_N ? C_INK : C_SEND_OFF;
        gfx_rrect(R_SEND.x, R_SEND.y, R_SEND.w, R_SEND.h, 7, sb);
        /* The arrow, written out and centred.
         *
         * Generated, it drew its ink across x 4..10 and y 4..10, so its centre was (7,7) inside a
         * 14px disc whose centre is (6.5,6.5) -- half a pixel low and half a pixel right, which is
         * exactly where it was reported to sit. At (+3,+3) the 7x7 glyph centres on (6,6): still
         * half a pixel out, because a 7px shape cannot centre in a 14px disc, but up and left,
         * where the arrow's own mass already leans. */
        static const char *ARROW[] = {
            "   #   ",
            "  ###  ",
            " ## ## ",
            "#  #  #",
            "   #   ",
            "   #   ",
            "   #   ",
        };
        /* +4 across, +3 down. Vertically it now reads centred; horizontally the half-pixel it
         * cannot avoid is better spent leaning right than left, which is where it was asked to
         * sit. */
        blit(R_SEND.x + 4, R_SEND.y + 3, ARROW, 7, C_BG, C_BG);
    }

}

/* Set the clock and say whether anything animated has actually changed since last time. The point
 * is to let the caller NOT redraw: this app renders a 320x240 framebuffer in software on a 288 MHz
 * core, so a redraw that changes nothing is pure heat. */
int app_set_now(unsigned ms) {
    unsigned prev = NOW_MS;
    NOW_MS = ms;
    if (BUSY) return 1;                       /* a running turn owns the screen */

    int moved = 0;
    if (CUR < 0 && !COMPOSE_N && !SEARCH_ON) {
        /* the rotating placeholder: only while it is the thing on screen */
        unsigned cycle = ASK_HOLD_MS + ASK_SLIDE_MS;
        unsigned a = prev % cycle, b = ms % cycle;
        int a_slide = a >= ASK_HOLD_MS, b_slide = b >= ASK_HOLD_MS;
        if (prev / cycle != ms / cycle) moved = 1;        /* the word changed */
        else if (b_slide) moved = 1;                      /* mid-slide, every frame counts */
        else if (a_slide != b_slide) moved = 1;           /* it just started or finished */
    }
    if (MARQ_AT >= 0) {
        unsigned ea = prev >= MARQ_T0 ? prev - MARQ_T0 : 0u;
        unsigned eb = ms   >= MARQ_T0 ? ms   - MARQ_T0 : 0u;
        if (eb > MARQ_HOLD_MS &&
            marq_off(ea, 1 << 20) != marq_off(eb, 1 << 20)) moved = 1;
    }
    return moved;
}

void app_draw(void) {
    gfx_clear(C_BG);
    if (SIDEBAR) draw_sidebar();
    draw_main();
    if (SEARCH_ON) draw_search();
    draw_cursor();          /* last, so nothing occludes it */
    gfx_present();
}

/* ---- input ------------------------------------------------------------------------------------ */
/* What the New chat control and ctrl+N both do. It does NOT create a session -- new_chat() runs on
 * the first send -- it returns to the empty screen and clears the box. Named because the button
 * and the chord were about to hold two copies of that, and the chord's copy called new_chat(),
 * which would have left an untitled empty session in the list on every press. */
static void start_new_chat(void) { CUR = -1; SCROLL = 0; COMPOSE_N = 0; COMPOSE[0] = 0; }
static void open_search(void)    { SEARCH_ON = 1; SQ_N = 0; SQ[0] = 0; SSEL = 0; run_search(); }

void app_event(const in_event *e) {
    if (e->kind == IN_MOVE)  { MX = e->x; MY = e->y; HOVER = e->hover; return; }
    if (e->kind == IN_SCROLL) {
        /* over the sidebar the wheel moves the SESSION LIST; over the pane it moves the transcript */
        if (SIDEBAR && !SEARCH_ON && inside(R_LIST, MX, MY)) {
            CHAT_SCROLL += (e->dy > 0) ? 1 : -1;
            if (CHAT_SCROLL < 0) CHAT_SCROLL = 0;
            return;
        }
        SCROLL += e->dy; if (SCROLL < 0) SCROLL = 0; return;
    }

    if (e->kind == IN_CLICK) {
        MX = e->x; MY = e->y;
        if (SEARCH_ON) {                       /* the sheet is modal: it eats clicks under it */
            int rows = NSHIT - SSCROLL; if (rows > SHEET_ROWS) rows = SHEET_ROWS;
            for (int i = 0; i < rows; i++)
                if (inside(R_SROW[i], MX, MY)) { CUR = SHIT[SSCROLL + i]; SCROLL = 0; SEARCH_ON = 0; return; }
            SEARCH_ON = 0;                     /* click outside a row closes, as on the web */
            return;
        }
        if (hit(R_EXIT, MX, MY)) { QUIT = 1; return; }
        if (BUSY && hit(R_SEND, MX, MY)) { ABORT = 1; return; }   /* Stop, mid-generation */
        if (hit(R_TOGGLE, MX, MY)) { SIDEBAR = !SIDEBAR; return; }
        if (hit(R_SEARCH, MX, MY)) { open_search(); return; }
        if (SIDEBAR) {
            if (hit(R_NEW, MX, MY)) { start_new_chat(); return; }
            for (int i = 0; i < NCHAT_ROWS; i++) {
                if (inside(R_TRASH[i], MX, MY)) { delete_chat(CHAT_AT[i]); return; }
                if (inside(R_CHAT[i], MX, MY))  { CUR = CHAT_AT[i]; SCROLL = 0; return; }
            }
        }
        if (hit(R_SEND, MX, MY) && COMPOSE_N && !BUSY) {
            app_request(COMPOSE, 0); COMPOSE_N = 0; COMPOSE[0] = 0; return;
        }
        return;
    }

    if (e->kind == IN_KEY) {
        int k = e->key;
        if (SEARCH_ON) {                       /* typing goes to the query, not the composer */
            if (k == K_ESC)   { SEARCH_ON = 0; return; }
            if (k == K_DOWN)  { if (SSEL + 1 < NSHIT) SSEL++; return; }   /* the sheet follows */
            if (k == K_UP)    { if (SSEL > 0) SSEL--; return; }
            if (k == K_ENTER) { if (NSHIT) { CUR = SHIT[SSEL]; SCROLL = 0; } SEARCH_ON = 0; return; }
            if (k == K_BACK)  { if (SQ_N) { SQ[--SQ_N] = 0; SSEL = 0; run_search(); } return; }
            if (k >= 32 && k < 127 && SQ_N < (int)sizeof SQ - 1) {
                SQ[SQ_N++] = (char)k; SQ[SQ_N] = 0; SSEL = 0; run_search();
            }
            return;
        }
        if (k == K_ESC)  {
            if (BUSY)         { ABORT = 1; return; }   /* interrupt first, never navigate away */
            else if (COMPOSE_N) { COMPOSE_N = 0; COMPOSE[0] = 0; return; }  /* then clear the box */
            else if (CUR >= 0)  { CUR = -1; return; }                       /* then go home */
            QUIT = 1; return;                                               /* then leave */
        }
        if (k == K_TAB)  { SIDEBAR = !SIDEBAR; return; }
        /* Chords work from anywhere, including mid-compose, which is the point of having them. */
        if (k == K_NEW)    { start_new_chat(); return; }
        if (k == K_SEARCH) { open_search(); return; }
        if (k == K_PANEL)  { SIDEBAR = !SIDEBAR; return; }
        if (k == K_BACK) { if (COMPOSE_N) COMPOSE[--COMPOSE_N] = 0; return; }
        if (k == K_ENTER) {
            if (COMPOSE_N && !BUSY) { app_request(COMPOSE, 0); COMPOSE_N = 0; COMPOSE[0] = 0; return; }
            /* an empty box on the home screen means "open what is selected" */
            if (CUR < 0 && NCHATS && SEL_ROW >= 0 && SEL_ROW < NCHATS) {
                CUR = SEL_ROW; SCROLL = 0; return;
            }
            return;
        }
        /* WITHOUT A POINTER THE ARROWS ARE THE ONLY WAY INTO THE LIST, so on the home screen they
         * move a selection through the sessions and ENTER opens the selected one. Inside a chat
         * there is nothing to select, so they keep scrolling the transcript. */
        if (CUR < 0 && NCHATS) {
            if (k == K_DOWN) { if (SEL_ROW + 1 < NCHATS) SEL_ROW++; return; }
            if (k == K_UP)   { if (SEL_ROW > 0) SEL_ROW--; return; }
        }
        if (k == K_DOWN) { SCROLL += 16; return; }
        if (k == K_UP)   { SCROLL -= 16; if (SCROLL < 0) SCROLL = 0; return; }
        if (k >= 32 && k < 127 && COMPOSE_N < (int)sizeof COMPOSE - 1) {
            COMPOSE[COMPOSE_N++] = (char)k; COMPOSE[COMPOSE_N] = 0;
        }
    }
}

/* ---- generation hooks -------------------------------------------------------------------------- */
static app_turn *pending;

/* Most-recently-used ordering, matching the web. A session reached the top of the list only when
 * it was CREATED; sending into an older one left it in place, which is not what "recents" means.
 * Moving the array element keeps CUR pointing at the same session. */
static void touch_chat(void) {
    if (CUR <= 0) return;
    app_chat tmp = CHATS[CUR];
    for (int i = CUR; i > 0; i--) CHATS[i] = CHATS[i - 1];
    CHATS[0] = tmp;
    CUR = 0;
    CHAT_SCROLL = 0;                 /* the session just moved to the top: show it */
}

void app_begin_turn(const char *question) {
    app_chat *c;
    if (CUR < 0) {
        char t[64]; snprintf(t, sizeof t, "%s", question);
        c = new_chat(t);
    } else { touch_chat(); c = &CHATS[CUR]; }   /* answering in an old session brings it forward */
    if (c->nturns >= MAX_TURNS) return;
    pending = &c->turn[c->nturns++];
    memset(pending, 0, sizeof *pending);
    snprintf(pending->q, sizeof pending->q, "%s", question);
    BUSY = 1;
}
/* Follow the tail while generating.
 *
 * Only app_stream_end() ever scrolled to the bottom, and clamp_scroll() can only REDUCE an offset --
 * so from the second turn onward the question, the status line and the streaming text all sat below
 * a ~10-line viewport until the turn finished. On a 22-second turn that is a frozen screen, which
 * defeats the entire point of having a status line. */
void app_stream_token(const char *piece) {
    SCROLL = 1 << 20;                   /* clamp_scroll pulls this back to the real bottom */
    if (!pending) return;
    int n = (int)strlen(pending->a), m = (int)strlen(piece);
    if (n + m < (int)sizeof pending->a - 1) { memcpy(pending->a + n, piece, (size_t)m); pending->a[n + m] = 0; }
}




/* Derive the compact summary from the finished turn: relation, the values that went in, and the
 * result the evaluator returned. Called at stream end, when <res> is present if it ever will be.
 * A turn with no result still summarises to its relation -- better than falling back to 90
 * characters of prose. */
static void summarise(app_turn *t, const char *formula, const char *values) {
    char res[24]; res[0] = 0;
    if (!formula) formula = "";
    const char *r = strstr(t->a, "<res>");
    if (r) {
        r += 5; while (*r == ' ') r++;
        int i = 0;
        while (*r && *r != '<' && i < (int)sizeof res - 1) res[i++] = *r++;
        while (i && res[i-1] == ' ') i--;
        res[i] = 0;
    }
    if (res[0] && values && *values) {
        char v[32]; snprintf(v, sizeof v, "%.28s", values);
        int n = (int)strlen(v);
        while (n && v[n-1] == ' ') v[--n] = 0;      /* callers pass a trailing separator */
        snprintf(t->sum, sizeof t->sum, "%.24s %s -> %.12s", formula, v, res);
    }
    else if (res[0])
        snprintf(t->sum, sizeof t->sum, "%.40s -> %.12s", formula ? formula : "", res);
    else
        snprintf(t->sum, sizeof t->sum, "%.60s", formula);
}

void app_finish_turn(const char *formula, const char *values) {
    if (pending) summarise(pending, formula ? formula : "", values ? values : "");
}

/* THREE TIERS, degrading rather than truncating:
 *   1. the most recent VERBATIM_TURNS turns keep their full question -- a follow-up almost always
 *      refers to these, and compacting them loses the phrasing it refers to;
 *   2. older turns collapse to their compact summary, ~1.6x cheaper (measured: 25.2 -> 15.5
 *      tokens per turn, so ~11 turns fit where ~6.6 did);
 *   3. whatever still does not fit is dropped, oldest first, SILENTLY. Asking again refreshes it.
 *
 * Newest-first assembly, then reversed: the budget must be spent on what the user just referred
 * to, not on the start of the session. */
#define VERBATIM_TURNS 2

int app_context(char *out, int cap, int budget) {
    out[0] = 0;
    if (CUR < 0) return 0;
    app_chat *c = &CHATS[CUR];
    int upto = c->nturns - (pending ? 1 : 0);
    if (upto <= 0) return 0;

    char parts[MAX_TURNS][200];
    int np = 0, used = 0;
    for (int i = upto - 1; i >= 0 && np < MAX_TURNS; i--) {
        app_turn *t = &c->turn[i];
        int recent = (i >= upto - VERBATIM_TURNS);
        char one[200];
        if (recent && t->q[0])       snprintf(one, sizeof one, "Earlier: %.90s ", t->q);
        else if (t->sum[0])          snprintf(one, sizeof one, "%.70s ", t->sum);
        else if (t->q[0])            snprintf(one, sizeof one, "Earlier: %.60s ", t->q);
        else continue;
        int l = (int)strlen(one);
        if (used + l > budget) {
            /* a verbatim turn that does not fit gets one chance in compact form before it is
             * dropped -- otherwise the newest turn could be lost while older ones survive */
            if (recent && t->sum[0]) {
                snprintf(one, sizeof one, "%.70s ", t->sum);
                l = (int)strlen(one);
                if (used + l > budget) break;
            } else break;
        }
        snprintf(parts[np++], sizeof parts[0], "%s", one);
        used += l;
    }
    int n = 0;
    for (int i = np - 1; i >= 0 && n < cap - 1; i--) {      /* reverse: oldest surviving first */
        int l = (int)strlen(parts[i]);
        if (n + l >= cap - 1) break;
        memcpy(out + n, parts[i], (size_t)l); n += l;
    }
    out[n] = 0;
    return n;
}

/* Polled by the generation loop, which is the only place that runs long enough to need it.
 * Cleared on read: an abort must not survive into the next question. */
int app_take_abort(void) { int a = ABORT; ABORT = 0; return a; }
int app_busy(void) { return BUSY; }

void app_status(const char *label, const char *mono) {
    SCROLL = 1 << 20;                   /* a phase change must be visible, not scrolled past */
    snprintf(STATUS, sizeof STATUS, "%s", label ? label : "");
    snprintf(STATUS_MONO, sizeof STATUS_MONO, "%s", mono ? mono : "");
}
void app_status_done(unsigned ms, const char *tool_call, const char *tool_result, int tool_ok) {
    if (!pending) return;
    /* The line that STAYS, and it is elapsed time only.
     *
     * It used to read "22.4s eval(150/12) -> 12.5", which is the runtime's vocabulary: `eval` is an
     * internal tool name and `150/12` is source syntax. It also put the answer's number on screen a
     * SECOND time, in a different notation, leaving the reader to reconcile the two. The call is
     * still executed, still logged, and still the reason the number is right; it is simply not the
     * reader's problem. The live line names the step while it runs, which is where that belongs.
     *
     * A refusal is different: nothing was computed, and saying so is not vocabulary, it is the
     * outcome. */
    if (tool_call && tool_call[0] && !tool_ok)
        snprintf(pending->sum, sizeof pending->sum, "%u.%us  tool refused",
                 ms / 1000, (ms % 1000) / 100);
    else
        snprintf(pending->sum, sizeof pending->sum, "%u.%us", ms / 1000, (ms % 1000) / 100);
    (void)tool_result;
    STATUS[0] = 0; STATUS_MONO[0] = 0;
}
int app_hit_stop(int x, int y) { return BUSY && inside(R_SEND, x, y); }

int app_hit_control(int x, int y) {
    if (SEARCH_ON) return 1;                    /* the sheet is modal: any click means something */
    if (inside(R_EXIT, x, y) || inside(R_SEND, x, y)) return 1;
    if (inside(R_NEW, x, y) || inside(R_SEARCH, x, y) || inside(R_TOGGLE, x, y)) return 1;
    for (int i = 0; i < NCHAT_ROWS; i++)
        if (inside(R_TRASH[i], x, y) || inside(R_CHAT[i], x, y)) return 1;
    return 0;
}

void app_stream_end(void) {
    if (pending) pending->done = 1;
    pending = 0; BUSY = 0; SCROLL = 1 << 20;
    persist();                       /* a finished turn is the thing worth not losing */
}

/* Clamp the scroll to the measured content height. Called from draw, because the height is only
 * known once the answer text has been laid out -- it changes on every streamed token. */
static void clamp_scroll(int content_h, int view_h) {
    int max = content_h - view_h;
    if (max < 0) max = 0;
    if (SCROLL > max) SCROLL = max;
    if (SCROLL < 0) SCROLL = 0;
}
