#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "app.h"
#include "chatstore.h"
#include "pickui.h"
#include "askparse.h"

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
    [P_DANGER] = HEX(0xD93025),
    /* A real tint, not another grey. C_SEL renders 17 levels off white, which reads on a
     * desktop panel and is close to invisible on a reflective calculator LCD in daylight. */
    [P_SELTEXT] = HEX(0xBBD6F2),
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
    /* Lighter than the light theme's red, not darker. Red on a dark ground loses contrast fast,
     * and a warning nobody can read is decoration. */
    [P_DANGER] = HEX(0xF87171),
    [P_SELTEXT] = HEX(0x2C4A6B),
    [P_EXIT_HOT] = HEX(0x4A2A28), [P_FIELD] = HEX(0x303030), [P_FIELD_LN] = HEX(0x303030), [P_SEND_OFF] = HEX(0x3B3B3B),
    [P_SHEET] = HEX(0x383838),   /* lifted OFF the page, since the scrim cannot sink it */
};

/* The clock decides only when the mode is AUTO. A negative hour means the clock could not be read;
 * light is returned then, because a wrong-but-legible default beats guessing dark on no evidence.
 * Day is 06:00-18:00, per the owner's spec. */
/* HOURS OFFSET FROM UTC. The RTC is a bare seconds counter and the calculator has no notion of a
 * timezone anywhere, so app_clock_hour() can only report UTC. "System" was therefore calling it
 * night at 10am for anyone far from Greenwich -- at UTC-10 the clock reads 20:00 and AUTO goes
 * dark in broad daylight, which is exactly what was reported.
 *
 * It cannot be derived, so it is asked for. In memory only: persisting it means a settings file,
 * and the theme is a one-tap fix if this is wrong. */
/* NAMED US ZONES, not a bare UTC number. "UTC-10" asks the reader to know their own offset;
 * "HST" is the thing they already call it.
 *
 * Standard and daylight are separate entries rather than a date calculation, because the device
 * cannot be trusted to know the date -- bench_rtc has never run, so even the epoch of that seconds
 * counter is unverified -- and a DST rule computed from an unverified clock is a guess wearing a
 * uniform. Picking PDT in July is one press and is honestly what the reader knows.
 *
 * Ordered west to east, so the minus key moves west and the plus key moves east. */
static const struct { const char *name; int off; } TZ[] = {
    { "HST",  -10 },      /* Hawaii, no daylight time */
    { "AKST",  -9 }, { "AKDT", -8 },
    { "PST",   -8 }, { "PDT",  -7 },
    { "MST",   -7 }, { "MDT",  -6 },
    { "CST",   -6 }, { "CDT",  -5 },
    { "EST",   -5 }, { "EDT",  -4 },
};
#define TZ_N ((int)(sizeof TZ / sizeof TZ[0]))
static int TZ_IDX = 3;                       /* PST: the middle of the populated range */

int app_tz(void)            { return TZ[TZ_IDX].off; }
const char *app_tz_name(void) { return TZ[TZ_IDX].name; }
int app_tz_index(void)      { return TZ_IDX; }

void app_set_tz_index(int i) {
    if (i < 0) i = 0;
    if (i >= TZ_N) i = TZ_N - 1;             /* clamp, not wrap: stepping off the end of a list
                                              * should stop, not silently jump to the far side */
    TZ_IDX = i;
    app_set_theme(app_theme());              /* re-resolve AUTO against the new local time */
}

int app_auto_is_dark(int hour) {
    if (hour < 0 || hour > 23) return 0;
    return !(hour >= 6 && hour < 18);
}
int app_theme(void) { return THEME_MODE; }

void app_set_theme(int mode) {
    THEME_MODE = mode;
    int dark = (mode == TH_DARK);
    if (mode == TH_AUTO) {
        int h = app_clock_hour();
        if (h >= 0) { h = (h + app_tz()) % 24; if (h < 0) h += 24; }
        dark = app_auto_is_dark(h);
    }
    const uint16_t *src = dark ? PAL_DARK : PAL_LIGHT;
    for (int i = 0; i < P_N; i++) TLM_PAL[i] = src[i];
}

/* Deliberately phrased as a person would ask, not as the store phrases a relation -- these are
 * examples of USE, and they have to still make sense once the relation flow is gone. */

/* THE PATHS ARE COPIED, NOT POINTED AT. This is not defensive style, it is a repair.
 *
 * device_app.c builds paths with dpath(), which returns a SHARED static buffer. Storing that
 * pointer meant PERSIST named whichever path was built most recently -- and rq_build() rebuilds it
 * with the MODEL path on the first send. From that moment persist() opened the model with "wb" and
 * wrote the chat store over it.
 *
 * Measured on the device: model4096.bin.tns was 21 bytes, and an empty chat store is exactly 21
 * bytes. The 96-byte version found earlier was a chat store holding one chat. It was never a USB
 * truncation. A sink that outlives its caller's buffer must own its own copy. */
static char PERSIST_BUF[96];
static const char *PERSIST;          /* NULL = do not persist (host harness) */

/* Called after EVERY change that could lose a conversation. Deliberately not called per token:
 * writing 141 KB at 2.68 tok/s would dominate generation, and a turn in progress is not worth
 * saving anyway -- it is the finished ones that matter. */
static void toast(const char *msg);
static void persist(void) {
    if (!PERSIST) return;
    /* A failed save is REPORTED. It used to be discarded, so a delete that could not be written
     * looked exactly like a delete that was -- until the next launch brought the session back. */
    if (chat_save(PERSIST, CHATS, NCHATS, CUR) != 0) toast("Could not save sessions");
}
static unsigned NOW_MS;            /* wall clock the UI ticks on; set by app_set_now */

/* THE CLIPBOARD. The device has no system clipboard, so this is it: one buffer, app-scoped.
 *
 * 512 bytes rather than the composer's 160, because copy has to be able to hold an answer that the
 * composer could never accept. Paste truncates at the composer's own limit instead of refusing,
 * since a paste that silently does nothing is the failure this project keeps writing rules about. */
/* 2048, not 512: select-all copies a whole conversation and the old size could not hold one. It
 * still truncates rather than refusing, and says so. */
static char CLIP[2048];
static char FEEDBACK_BUF[96];
static const char *FEEDBACK;        /* append-only rating log; 0 on the host */
/* Copied for the same reason as PERSIST above: dpath()'s buffer is shared, and this call site sits
 * one line after app_set_persist(), which is precisely how it clobbered it. */
void app_set_feedback(const char *path) {
    if (!path) { FEEDBACK = 0; return; }
    snprintf(FEEDBACK_BUF, sizeof FEEDBACK_BUF, "%s", path);
    FEEDBACK = FEEDBACK_BUF;
}

/* A short confirmation for actions that leave no visible trace. Copy is the case that needs it:
 * without a receipt, pressing it looks exactly like pressing nothing. */
static char TOAST[48];
static unsigned TOAST_UNTIL;
#define TOAST_MS 1600

/* Appends one rating to the feedback log. The highlight is a receipt for the click; THIS is the
 * record, and it is what makes a thumb worth pressing at all on a device nobody exports from.
 *
 * Append-only and flushed per press: the calculator loses power without warning, and a rating that
 * only exists in a buffer is a rating that did not happen. */
static void feedback_write(int up, const char *q, const char *a) {
    if (!FEEDBACK) return;                    /* host tests have nothing to write to */
    FILE *f = fopen(FEEDBACK, "a");
    if (!f) return;
    fprintf(f, "%s\t%s\t%s\n", up > 0 ? "up" : "down", q ? q : "", a ? a : "");
    fclose(f);
}

static void toast(const char *msg) {
    snprintf(TOAST, sizeof TOAST, "%s", msg);
    TOAST_UNTIL = NOW_MS + TOAST_MS;
}
/* Returns 1 when something was copied, so the caller can say which. */
static int clip_set(const char *s) {
    if (!s || !*s) return 0;
    snprintf(CLIP, sizeof CLIP, "%s", s);
    return 1;
}

void app_set_persist(const char *path) {
    if (!path) { PERSIST = 0; return; }
    snprintf(PERSIST_BUF, sizeof PERSIST_BUF, "%s", path);
    PERSIST = PERSIST_BUF;
    path = PERSIST_BUF;
    int cur = -1;
    int n = chat_load(path, CHATS, MAX_CHATS, &cur);
    if (n > 0) { NCHATS = n; CUR = cur; }
}
static char COMPOSE[160];          /* what is being typed */
static int  COMPOSE_N;
/* The composer's own select-all. A TEXT FIELD's select-all, which is a different thing from the
 * transcript's: it is what you press before retyping or deleting a draft, and the next keystroke
 * is expected to REPLACE what is highlighted rather than append to it. */
static int COMPOSE_SEL;
/* ONE way to empty the box.
 *
 * COMPOSE_N, the string, and the selection flag are three pieces of ONE state, and seven separate
 * places emptied the field by clearing two of them. Each would have left a highlight standing over
 * an empty box, and after that the next keystroke would "replace a selection" that no longer had
 * any text in it. Three earlier defects in this file were state a reset forgot; this removes the
 * chance rather than adding a fourth careful call site. */
/* WHERE THE NEXT CHARACTER GOES. Without it the field could only be edited from the end -- a typo
 * three characters back meant deleting everything after it, which is what "I have to delete stuff
 * to write something again" was. Kept in [0, COMPOSE_N] by every path that touches the buffer, and
 * reset here with the rest of the state for the reason this function exists. */
static int COMPOSE_C;
static int TEXT_X, TEXT_Y, TEXT_W;   /* the composer's last laid-out text origin */

/* THE CHARACTER PALETTE, on the menu key.
 *
 * WHAT IT IS NOT: a function menu. The OS's menu key opens actions / algebra / calculus, and the
 * temptation is to mirror that. This engine cannot honour it -- the runtime dispatches `eval` and
 * the CORPUS TRAINS EXACTLY ONE FUNCTION, 197,608 of 197,608 calls, so the model never emits diff,
 * integ or solve however they are offered. A menu with an integral on it would promise arithmetic
 * this model does not do.
 *
 * WHAT IT IS: the characters a question needs that the keypad cannot reach. The store's own
 * spelling decides the list -- it writes `pi`, `theta`, `Delta`, `sqrt(` as ASCII, so those are
 * what get inserted, not glyphs the tokenizer has never seen. */
/* CATEGORISED, because one flat grid of sixteen could not hold what a physics question needs and
 * the user asked for "pretty much any type of mathematical thing". TAB cycles the categories.
 *
 * EVERY ENTRY IS SOMETHING THE STORE OR THE KNOWLEDGE TIER ACTUALLY WRITES. Measured over
 * corpus/store_clean.json, the only functions any of the 164 formulas use are sqrt, sin, asin and
 * cos -- so those four are offered and nothing else. Same reasoning as the note above: a tile is a
 * promise, and a palette with log( or integral on it would promise arithmetic this model has never
 * been trained to emit. Units and subscripts are the ones the store measures as most frequent. */
/* A122. A TILE MAY DISPLAY ONE THING AND INSERT ANOTHER. `lab` is NULL for every category that
 * existed before this, where the glyph and the insertion are the same string. CALC needs the split:
 * the tile reads as an integral sign and what goes into the question is the words the model was
 * trained on. */
typedef struct { const char *name; const char *const *it; const char *const *lab; int n; } pal_cat;

static const char *const PAL_GREEK[] = {
    "alpha", "beta", "gamma", "delta", "epsilon",
    "theta", "lambda", "mu", "nu", "pi",
    "rho", "sigma", "tau", "phi", "omega",
    "Delta", "Theta", "Sigma", "Phi", "Omega",
};
static const char *const PAL_MATH[] = {
    "_", "^", "^2", "^3", "sqrt(",
    "(", ")", "*", "/", "+",
    "-", "=", ".", ",", "e-",
    "sin(", "cos(", "asin(", "<", ">",
};
static const char *const PAL_VARS[] = {
    "_0", "_1", "_2", "_i", "_f",
    "_x", "_y", "_z", "_t", "_n",
    "_net", "_max", "_min", "_tot", "_rms",
    "_CM", "_avg", "_in", "_out", "_eff",
};
static const char *const PAL_UNITS[] = {
    "m", "s", "kg", "N", "J",
    "W", "A", "V", "C", "K",
    "Hz", "Pa", "T", "ohm", "mol",
    "m/s", "m/s^2", "N/m", "kg/m^3", "W/m^2",
};
static const char *const PAL_CONST[] = {
    "c", "g", "h", "hbar", "k_B",
    "N_A", "R", "G", "epsilon_0", "mu_0",
    "q_e", "m_e", "m_p", "sigma", "atm",
};

/* A122. CALCULUS, AND THE COMMENT ABOVE THIS BLOCK USED TO FORBID IT.
 *
 * It said: "the CORPUS TRAINS EXACTLY ONE FUNCTION, 197,608 of 197,608 calls, so the model never
 * emits diff, integ or solve however they are offered. A menu with an integral on it would promise
 * arithmetic this model does not do." That was true when it was written and it EXPIRED. Measured on
 * the shipped corpus: 24,080 of 215,531 tool calls (10.4%) are solve, diff or integ, and on device
 * the model emits the right `solve` call on 60 of 60 probed relations and the right `diff` call on
 * 50 of 50. The promise is now one the model keeps.
 *
 * THE TILES ARE THE PHRASES THE CORPUS ACTUALLY TRAINS, measured rather than invented -- the same
 * rule the categories above follow ("every entry is something the store or the knowledge tier
 * actually writes"). Frequencies over the 24,080 calculus and rearrangement questions:
 *
 *     solve 18.1%   derivative of 13.8%   with respect to 12.3%   rearrange 7.5%
 *     differentiate 7.4%   integrate 6.9%   from 4.5%   isolate 3.8%
 *     antiderivative 2.3%   integral of 2.3%
 *
 * THE INTEGRAL SIGN IS A REAL GLYPH. U+222B is in font_data.h (checked, not assumed), so the tile
 * reads as an integral and inserts the WORDS -- the question reaches the model as the student typed
 * it, and "integral of" is what 2.3% of the training questions say while the glyph appears in none.
 *
 * NO CONTOUR INTEGRAL. U+222E is absent from the font AND there is no closed-path integration in
 * TOOL_SPEC's seven functions, so a tile for it would promise arithmetic that does not exist --
 * which is the exact failure the comment above this one was written to prevent. Recorded here so
 * the next person does not have to rediscover both halves.
 *
 * NO PARTIAL DERIVATIVE either, though U+2202 IS in the font: `diff` is single-variable and the
 * corpus trains no partial. The glyph being available is not the test. */
static const char *const PAL_CALC[] = {
    "integral of ", "integrate ", "antiderivative of ", "derivative of ", "differentiate ",
    "with respect to ", "from ", "to ", "area under ",
    "solve ", "rearrange ", "isolate ",
};
static const char *const PAL_CALC_LAB[] = {
    "∫",       "∫ dx",  "antider",           "d/dx",          "differ",
    "w.r.t.",        "from",     "to",  "area",
    "solve",         "rearr",       "isolate",
};

#define CAT(a) { #a, PAL_##a, (int)(sizeof PAL_##a / sizeof PAL_##a[0]) }
static const pal_cat PAL_CAT[] = {
    { "GREEK", PAL_GREEK, 0, (int)(sizeof PAL_GREEK / sizeof PAL_GREEK[0]) },
    { "MATH",  PAL_MATH,  0, (int)(sizeof PAL_MATH  / sizeof PAL_MATH[0]) },
    { "CALC",  PAL_CALC,  PAL_CALC_LAB, (int)(sizeof PAL_CALC / sizeof PAL_CALC[0]) },
    { "SUB",   PAL_VARS,  0, (int)(sizeof PAL_VARS  / sizeof PAL_VARS[0]) },
    { "UNITS", PAL_UNITS, 0, (int)(sizeof PAL_UNITS / sizeof PAL_UNITS[0]) },
    { "CONST", PAL_CONST, 0, (int)(sizeof PAL_CONST / sizeof PAL_CONST[0]) },
};
#undef CAT
#define CAT_N   ((int)(sizeof PAL_CAT / sizeof PAL_CAT[0]))
#define SYM_MAX 25                  /* tiles a category may hold; R_SYM is sized to it */
#define SYM_COLS 5
static int SYM_ON, SYM_SEL, SYM_CAT;
static gfx_rect R_SYMSHEET;   /* A123: the palette's own sheet, so a click inside it cannot close it */
static gfx_rect R_SYM[SYM_MAX];
static gfx_rect R_CAT[CAT_N];

/* The live category's tiles. One accessor so the drawing, the keys and the touch path cannot
 * disagree about which list is on screen -- the class of defect that gave this app two rankers. */
static const pal_cat *pal_cur(void) {
    if (SYM_CAT < 0 || SYM_CAT >= CAT_N) SYM_CAT = 0;
    return &PAL_CAT[SYM_CAT];
}
static int pal_n(void) { const pal_cat *c = pal_cur(); return c->n > SYM_MAX ? SYM_MAX : c->n; }
static const char *pal_at(int i) { return pal_cur()->it[i]; }
/* What the TILE shows. Falls back to the insertion, which is what every pre-A122 category wants. */
static const char *pal_lab(int i) {
    const pal_cat *c = pal_cur();
    return (c->lab && c->lab[i]) ? c->lab[i] : c->it[i];
}

static void compose_clear(void) { COMPOSE_N = 0; COMPOSE[0] = 0; COMPOSE_SEL = 0; COMPOSE_C = 0; }

/* Insert at the caret, the one path both a keystroke and the palette use. */
static void compose_insert_str(const char *t) {
    int L = (int)strlen(t);
    if (COMPOSE_N + L >= (int)sizeof COMPOSE) return;
    if (COMPOSE_SEL) compose_clear();
    if (COMPOSE_C < 0) COMPOSE_C = 0;
    if (COMPOSE_C > COMPOSE_N) COMPOSE_C = COMPOSE_N;
    memmove(COMPOSE + COMPOSE_C + L, COMPOSE + COMPOSE_C, (size_t)(COMPOSE_N - COMPOSE_C));
    memcpy(COMPOSE + COMPOSE_C, t, (size_t)L);
    COMPOSE_C += L; COMPOSE_N += L; COMPOSE[COMPOSE_N] = 0;
}

static void clip_paste(void) {
    if (!CLIP[0]) { toast("Clipboard is empty"); return; }
    /* Pasting over a selection replaces it, like typing does. */
    if (COMPOSE_SEL) compose_clear();
    int room = (int)sizeof COMPOSE - 1 - COMPOSE_N;
    if (room <= 0) { toast("Message box is full"); return; }
    int n = (int)strlen(CLIP);
    int take = n < room ? n : room;
    memcpy(COMPOSE + COMPOSE_N, CLIP, (size_t)take);
    COMPOSE_N += take; COMPOSE[COMPOSE_N] = 0;
    COMPOSE_C = COMPOSE_N;   /* a paste leaves the caret after what it pasted */
    /* Says so when it could not take all of it. Truncating quietly is how a paste looks like it
     * worked and is not what was copied. */
    if (take < n) toast("Pasted, trimmed to fit");
    else          toast("Pasted");
}

static int  BUSY;

/* live status, drawn above the streaming answer */
static char STATUS[48], STATUS_MONO[40];

/* hit regions, recomputed every frame so hover testing and click handling can never disagree
 * about where something is -- they read the same rectangles. */
static gfx_rect R_TOGGLE, R_NEW, R_CHAT[MAX_CHATS], R_TRASH[MAX_CHATS], R_FIELD, R_SEND;
static gfx_rect R_EXIT;
/* Copy / thumb-up / thumb-down, per turn. Recorded every frame, like every other rect here, so
 * hover testing and click handling cannot disagree about what exists. */
static int SEL_TURN = -1, SEL_SPAN = -1, SEL_A, SEL_B, SEL_ANCHOR, DRAGGING;
/* SELECT ALL is a MODE, not a very long range. A range lives inside one wrapped block, and the
 * whole conversation is many of them -- questions and answers interleaved, each laid out
 * separately. Expressing "everything" as offsets would mean inventing a coordinate space that
 * nothing draws in. As a mode, every block simply renders fully highlighted and the copy walks the
 * turns in order. */
static int SEL_ALL;

static gfx_rect R_ANS[MAX_TURNS];      /* each answer's drawn block, for probing and for ctrl+c */
static gfx_rect R_ACT[MAX_TURNS][3];
static gfx_rect R_QACT[MAX_TURNS];     /* copy, under each question bubble */
static int NACT_ROWS;
/* Reserved under EVERY answer, painted only on hover. Reserving it unconditionally is the point:
 * revealing a control that also takes space would reflow the transcript under the pointer, so the
 * thing you were reaching for moves as you reach for it. */
#define ACT_H  16
#define QACT_H 16              /* reserved under every question bubble, same reason */
#define ACT_SZ 14
/* Declared up here with the other controls rather than beside the search sheet's state, because
 * hit testing has to see every control that competes for a click in one place. */
static gfx_rect R_SEARCH;
static gfx_rect R_SET_THEME, R_SET_QUIT, R_SET_TZM, R_SET_TZP;   /* rows inside the settings sheet */
static int SETTINGS_ON;

/* 10, and resolved by NEAREST rather than by first match.
 *
 * 5 was still too small. The obstacle to simply raising it is that the three sidebar icons sit on a
 * 28px pitch with 24px plates, so anything past 2px of padding makes neighbouring boxes overlap --
 * and with a first-match test the control checked earliest silently wins the whole overlap, which
 * biases every near-miss towards one icon.
 *
 * So the question a click asks is not "am I inside this box" but "which control am I closest to,
 * and is it close enough". Overlap stops mattering, because the midpoint between two neighbours
 * always resolves to the nearer one. */
#define HIT_PAD 10

/* Squared distance from a point to a rect; 0 inside. Squared, to keep it integer. */
static int rect_d2(gfx_rect r, int x, int y) {
    int dx = x < r.x ? r.x - x : (x >= r.x + r.w ? x - (r.x + r.w - 1) : 0);
    int dy = y < r.y ? r.y - y : (y >= r.y + r.h ? y - (r.y + r.h - 1) : 0);
    return dx * dx + dy * dy;
}
static int inside_pad(gfx_rect r, int x, int y, int pad) {
    return rect_d2(r, x, y) <= pad * pad;
}

/* Every control that competes for a click, in one place, so hover and hit testing cannot disagree
 * about what exists. */
static int nearest_is(gfx_rect r, int x, int y) {
    if (rect_d2(r, x, y) > HIT_PAD * HIT_PAD) return 0;
    /* No need to exclude r itself: its own distance equals `best`, and the test below is strict. */
    int best = rect_d2(r, x, y);
    gfx_rect *cand[] = { &R_NEW, &R_SEARCH, &R_TOGGLE, &R_EXIT, &R_SEND };
    for (int i = 0; i < (int)(sizeof cand / sizeof cand[0]); i++) {
        if (cand[i]->w <= 0) continue;
        int d = rect_d2(*cand[i], x, y);
        if (d < best) return 0;                /* something else is closer */
    }
    return 1;
}
static int hit(gfx_rect r, int x, int y) { return nearest_is(r, x, y); }
/* Hover marquee for session titles.
 *
 * Titles are ellipsised at ~64px, which for a question is a few words -- often not enough to tell
 * two sessions apart. On hover the full title scrolls left, stops at its end and stays there;
 * moving away resets it. MARQ_AT is the chat index under the cursor and MARQ_T counts draws since
 * it arrived, so the animation is driven by the redraw loop and needs no clock. */
/* The app's millisecond clock, fed by app_set_now(). Declared ahead of BOTH animations because
 * each reads it, and the marquee's helper sits above the placeholder's table. */
/* Declared above, beside the toast that needs it. */

static int FIELD_FOCUS;      /* the composer is the typing target and says so */
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

/* EXAMPLE PROMPTS. The empty screen carried suggestion rows once and they were REMOVED, because
 * they ellipsised at this width -- "A car goes 150 m in 12 s. Find ..." -- so the one thing they
 * existed to do, show what a question looks like, was the one thing they could not do.
 *
 * These are vetted by tools/eval/promptcheck.c against THREE failure modes, measured rather than
 * eyeballed: each fits the pane at F_SM (162-183 px against 268), each is found by the shortlist
 * at rank #1, and -- the one I missed the first time -- each ASKS FOR A QUANTITY.
 *
 * THE FIRST SET DID NOT. They were "hooke's law, k = 250, x = 0.08" and the assembled prompt was
 * <q>hooke's law k = 250, x = 0.08.</q>: a relation NAME, asking for nothing. The model refused,
 * correctly, and it read as a model failure on the device. I had vetted pixel width and shortlist
 * rank -- two structural properties -- and never read the assembled document, which is the step
 * the project log makes mandatory before anything ships. Training questions are VERB + QUANTITY:
 * "Calculate frequency.", "Find path length difference.", "determine I_S".
 *
 * AND EACH IS VERIFIED END TO END, not merely vetted for width and rank. "Find kinetic energy.
 * m = 2, v = 3" was here and is not any more: the tool call and the runtime result are correct (9)
 * and the prose says "9 m" -- the variable name for mass, not the record's J -- DETERMINISTICALLY,
 * on every run, under greedy. An example that is always wrong is a bad example whatever its rank.
 * The four that replaced or survived it are right: work 30 J, pressure 2e+04 Pa, momentum
 * 1.8e+04 kg*m/s, density 8000 kg/m^3. Model unit accuracy overall is 98.4% (61/62) against a
 * corpus that is 100.0% (30,824/30,824), so KE is a deterministic instance of a ~1.6% rate.
 *
 * AND NO DEFINITE ARTICLE, which is measured and not a style choice. On 49 records, greedy, with
 * the givens and the record span held identical and ONLY the article varied:
 *
 *     Find <quantity>.                     0.0% refused
 *     Find the <quantity>.                12.2% refused
 *     Find the <full record name>.        25.4% refused
 *
 * The model is brittle to a function word carrying no information -- the corpus's entire question
 * vocabulary is 249 words, so a surface a student will produce naturally is not covered. See
 * docs/RESULT_ASK_SURFACE.md. Three families -- energy, pressure, momentum -- because the format
 * is what they teach: a question naming what you want, then `name = value` for what you know.
 *
 * Tapping one LOADS it into the box rather than sending it. The format is the lesson; sending it
 * immediately would hide the very thing being demonstrated. */
/* A127. THE EXAMPLES ADVERTISE THE RANGE, not one capability three times.
 *
 * All three used to be "Find X. given..." -- the compute case. A student reading them would never
 * learn that this app explains a relation, rearranges one, or differentiates it, and those are the
 * capabilities the last two months of work went into. The first screen is where a student decides
 * what the thing is for.
 *
 * MEASURED: all four resolve confidently against the shipped store (ask_confident, build/store.tns).
 * An example that refuses is worse than no example, which is the same rule ASK_ABOUT now follows,
 * and test_palette asserts it for both. */
/* A135. TRY_Q used to live here -- four tappable example questions under the composer. Removed
 * after device use: they sat directly below the box, so a click that fell short of the box ran a
 * question the student had not asked, and that happened repeatedly. The bottom key row already
 * names menu, symbols, catalog and formulas, which covers the discoverability this was for. */

/* A126. EVERY TOPIC HERE IS ONE THE STORE ACTUALLY COVERS, and that was MEASURED rather than
 * assumed. The placeholder rotates "Ask me about {topic}" -- it is an invitation, and an invitation
 * to something the app refuses is worse than no invitation.
 *
 * The old list carried 38 topics of which SEVENTEEN resolved to nothing: algebra, trigonometry,
 * logarithms, matrices, sequences, quadratics, exponentials, limits, optimization, averages,
 * distributions, regression, motion, circuits, magnetism and more. A student who followed the
 * prompt and typed "distributions" got a refusal. Others resolved but misleadingly -- "geometry"
 * returned *non-Euclidean geometry*, "series" returned *in series* (the circuit sense).
 *
 * The brief is explicit that this is not a general maths tool: "we don't need all of math, we need
 * the math that is part of physics." The pure-maths topics are gone for that reason and not only
 * because they miss.
 *
 * Measured with ask_confident against the shipped store: 29 of 30 candidates resolve, and the one
 * that did not ("a lens") is not here. Re-measure this list whenever the store changes -- a topic
 * that stops resolving is an invitation the app can no longer accept. */
static const char *ASK_ABOUT[] = {
    "velocity", "acceleration", "force", "friction",
    "momentum", "energy", "work", "power",
    "gravity", "circular motion", "waves", "sound",
    "light", "optics", "heat", "pressure",
    "a circuit", "resistance", "a magnetic field", "current",
    "voltage", "torque", "kinetic energy", "potential energy",
    "a spring", "refraction", "density", "impulse",
    "frequency",
    /* DERIVATIVES, INTEGRALS AND REARRANGEMENT ARE NOT HERE, and their absence is deliberate. The
     * app does all three -- on a RELATION -- but the placeholder invites a bare-topic question, and
     * "what is derivatives" resolves to nothing. Advertising them here would be the same
     * overpromise the seventeen maths topics were. They are advertised where they can be phrased
     * correctly: the CALC tab of the symbol palette, and the help section in Settings. */
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

static int inside(gfx_rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void app_init(void) {
    SEL_ROW = 0;
    app_set_theme(THEME_MODE);   /* fill the palette before anything draws */
    NCHATS = 0; CUR = -1; SCROLL = 0; compose_clear();
    /* Selection is per-transcript, so it cannot survive a reset. Three separate defects in this
     * file have been state that app_init forgot -- the sidebar, the marquee clock, and the modal
     * settings flag, which ate the next test's clicks. */
    SEL_TURN = SEL_SPAN = -1; SEL_A = SEL_B = 0; DRAGGING = 0; SEL_ALL = 0; COMPOSE_SEL = 0;
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

/* ---- selection -------------------------------------------------------------------------------
 * A selection is one contiguous byte range inside ONE text span of ONE answer.
 *
 * Not "anywhere on screen": an answer is a sequence of spans, each wrapped independently, and a
 * range spanning two of them would have to describe text that is never laid out as one block. The
 * copy BUTTON covers the whole-answer case, so the drag only has to cover the part of it a reader
 * would pick out by hand. */

/* Draws an answer, and optionally reports the span and byte offset under a pixel.
 *
 * ONE function for both, because a probe that walked spans differently than the draw would report
 * the offset of a character that is not there. `draw` 0 measures, exactly as before. */
static int draw_answer_ex(int x, int y, int w, const char *raw, int draw,
                          int sel_span, int sel_a, int sel_b,
                          int probe_x, int probe_y, int *out_span, int *out_off) {
    int lh = gfx_font_h(F_UI) + 2, cy = y;
    const char *p = raw;
    char buf[512]; sp_kind k;
    int si = 0;
    if (out_span) { *out_span = -1; *out_off = -1; }
    while ((p = span_next(p, &k, buf, sizeof buf)) != 0) {
        if (k == SP_TEXT) {
            const char *s = buf; while (*s == ' ') s++;
            if (*s) {
                static char disp[640];
                to_display(s, disp, sizeof disp);
                int n;
                /* -2 is select-all: every span, whole. A sentinel rather than a range because
                 * "all" has no meaningful start and end in a coordinate space that only exists
                 * inside one block. */
                int all = (sel_span == -2);
                if (draw && (all || (si == sel_span && sel_b > sel_a)))
                    n = gfx_text_wrap_sel(x, cy, disp, F_UI, C_INK, C_BG, w, lh,
                                          all ? 0 : sel_a, all ? (int)strlen(disp) : sel_b,
                                          C_SELTEXT);
                else
                    n = gfx_text_wrap(x, cy, disp, F_UI, C_INK, C_BG, w, lh, draw);
                if (out_span && probe_y >= cy && probe_y < cy + n * lh) {
                    int off = gfx_text_wrap_hit(x, cy, disp, F_UI, w, lh, probe_x, probe_y);
                    if (off >= 0) { *out_span = si; *out_off = off; }
                }
                cy += n * lh;
                si++;
            }
        }
        if (!*p) break;
    }
    return cy - y;
}
static int draw_answer(int x, int y, int w, const char *raw, int draw) {
    return draw_answer_ex(x, y, w, raw, draw, -1, 0, 0, 0, -1, 0, 0);
}

/* The display text of the idx'th text span, derived the same way draw_answer_ex derives it. */
static const char *span_disp(const char *raw, int idx) {
    static char disp[640];
    const char *p = raw; char buf[512]; sp_kind k; int si = 0;
    while ((p = span_next(p, &k, buf, sizeof buf)) != 0) {
        if (k == SP_TEXT) {
            const char *s = buf; while (*s == ' ') s++;
            if (*s) {
                if (si == idx) { to_display(s, disp, sizeof disp); return disp; }
                si++;
            }
        }
        if (!*p) break;
    }
    return 0;
}
static int sel_active(void) { return SEL_ALL || (SEL_TURN >= 0 && SEL_B > SEL_A); }
static void sel_clear(void) { SEL_TURN = SEL_SPAN = -1; SEL_A = SEL_B = 0; SEL_ALL = 0; }
static const char *sel_all_text(void);
static const char *sel_text(void) {
    static char out[512];
    if (SEL_ALL) return sel_all_text();
    if (!sel_active() || CUR < 0 || SEL_TURN >= CHATS[CUR].nturns) return 0;
    const char *d = span_disp(CHATS[CUR].turn[SEL_TURN].a, SEL_SPAN);
    if (!d) return 0;
    int dl = (int)strlen(d);
    int a = SEL_A, b = SEL_B;
    if (a < 0) a = 0;
    if (b > dl) b = dl;                        /* the text can change under a stale selection */
    if (b <= a) return 0;
    int n = b - a;
    if (n > (int)sizeof out - 1) n = (int)sizeof out - 1;
    memcpy(out, d + a, (size_t)n); out[n] = 0;
    return out;
}
/* The whole conversation as text, in the order it is on screen.
 *
 * Labelled, because a transcript pasted into a new session without them is one wall of prose in
 * which nobody can tell the question from the answer. Truncation is reported by the caller. */
static const char *sel_all_text(void) {
    static char out[sizeof CLIP];
    if (CUR < 0) return 0;
    int n = 0;
    app_chat *c = &CHATS[CUR];
    for (int i = 0; i < c->nturns && n < (int)sizeof out - 1; i++) {
        n += snprintf(out + n, sizeof out - (size_t)n, "You: %s\n", c->turn[i].q);
        if (n >= (int)sizeof out - 1) break;
        if (c->turn[i].a[0])
            n += snprintf(out + n, sizeof out - (size_t)n, "TLM: %s\n\n", c->turn[i].a);
    }
    out[sizeof out - 1] = 0;
    return n > 0 ? out : 0;
}
static const char *answer_under_pointer(void) {
    if (CUR < 0) return 0;
    for (int i = 0; i < CHATS[CUR].nturns && i < MAX_TURNS; i++)
        if (R_ANS[i].w > 0 && inside(R_ANS[i], MX, MY)) return CHATS[CUR].turn[i].a;
    return 0;
}
/* Locates (turn, span, offset) under a pixel, using each answer's RECORDED draw origin so the
 * probe lays the text out exactly where it was painted. */
static int sel_probe(int x, int y, int *turn, int *span, int *off) {
    if (CUR < 0) return 0;
    for (int i = 0; i < CHATS[CUR].nturns && i < MAX_TURNS; i++) {
        if (R_ANS[i].w <= 0 || !inside(R_ANS[i], x, y)) continue;
        int sp, of;
        draw_answer_ex(R_ANS[i].x, R_ANS[i].y, R_ANS[i].w, CHATS[CUR].turn[i].a, 0,
                       -1, 0, 0, x, y, &sp, &of);
        if (sp >= 0) { *turn = i; *span = sp; *off = of; return 1; }
    }
    return 0;
}
static void sel_begin(int x, int y) {
    int tn, sp, of;
    if (!sel_probe(x, y, &tn, &sp, &of)) { sel_clear(); return; }
    SEL_TURN = tn; SEL_SPAN = sp; SEL_ANCHOR = of; SEL_A = SEL_B = of;
}
static void sel_extend(int x, int y) {
    int tn, sp, of;
    if (SEL_TURN < 0) { sel_begin(x, y); return; }
    if (!sel_probe(x, y, &tn, &sp, &of)) return;      /* off the text: keep what we have */
    if (tn != SEL_TURN || sp != SEL_SPAN) return;     /* drags do not cross blocks */
    SEL_A = of < SEL_ANCHOR ? of : SEL_ANCHOR;
    SEL_B = of < SEL_ANCHOR ? SEL_ANCHOR : of;
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
static gfx_rect R_SROW[MAX_CHATS];

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
/* The three answer actions, written out for the same reason as the lens and the cursor: every
 * shape in this file that was generated by a loop has lost part of itself at least once. At 11px a
 * thumb is four or five decisions wide, and a loop makes all of them badly. */
static const char *IC_COPY[] = {
    "   ########",
    "   #      #",
    "   #      #",
    "#######   #",
    "#     #   #",
    "#     #   #",
    "#     #####",
    "#     #    ",
    "#     #    ",
    "#######    ",
};
static const char *IC_UP[] = {
    "     ##    ",
    "    #  #   ",
    "    #  #   ",
    "    #  #   ",
    " ####  ####",
    " #        #",
    " #        #",
    " #        #",
    " ##########",
};
static const char *IC_DOWN[] = {
    " ##########",
    " #        #",
    " #        #",
    " #        #",
    " ####  ####",
    "    #  #   ",
    "    #  #   ",
    "    #  #   ",
    "     ##    ",
};

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
/* AN EXCLAMATION MARK, not a gear.
 *
 * A gear is a ring, eight teeth and a hole, and at 16px on this panel there are not enough pixels
 * for any of them to read: the teeth merge into the ring and the whole thing closes up. This is a
 * bar and a dot, which is legible at any size and still says "there is something to read here".
 *
 * Same 16x16 envelope as the other glyphs, centred on (11.5, 11.5) like theirs. */
static void info_icon(int x, int y, uint16_t c) {
    /* 1px wide, spanning y+4..y+19 -- the SAME 16-row envelope as the pencil, lens and panel.
     *
     * Two separate properties, and the earlier versions each got one of them wrong. WEIGHT is set
     * by stroke width: its neighbours are 1px outlines, so a 2px bar was the heaviest mark on the
     * row while being the smallest glyph. HEIGHT is set by the envelope, and shrinking it to 12 to
     * fix the weight made it a short mark in a row of tall ones -- solving the first problem with
     * the wrong dial. 1px thin and full height gets both.
     *
     * 11 stem, 3 gap, 2 dot. The gap is wider than the dot because a period reads as separated
     * from the stem, not merely below it; at a 1px gap the two fuse into one bar.
     *
     * Centre lands on (11.0, 11.5). The vertical matches the others exactly; the horizontal is
     * half a pixel left because an odd width cannot straddle a half-pixel, and 2px to square it is
     * what made this too heavy twice. */
    gfx_fill(x + 11, y + 4,  1, 11, c);      /* stem */
    gfx_fill(x + 11, y + 18, 1, 2,  c);      /* dot  */
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
                /* TWO hover levels, because they answer different questions. The row being hot
                 * says "this is the session you are pointing at"; the icon being hot says "release
                 * here and it is gone". Sharing one grey plate for both meant the only warning
                 * before a destructive click was a plate the row already had. */
                int on_icon = inside(R_TRASH[r], MX, MY);
                uint16_t ink = on_icon ? C_DANGER : C_INK2;
                gfx_rrect(tx, ty, 14, 14, 3, on_icon ? C_TRASH_HOT : bg);
                gfx_hline(tx + 3, ty + 4, 9, ink);         /* lid */
                gfx_hline(tx + 6, ty + 2, 3, ink);         /* handle */
                gfx_vline(tx + 4, ty + 5, 7, ink);         /* body sides */
                gfx_vline(tx + 10, ty + 5, 7, ink);
                gfx_hline(tx + 4, ty + 11, 7, ink);        /* base */
                gfx_vline(tx + 7, ty + 6, 5, ink);         /* tine */
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
        gfx_rrect(R_EXIT.x, R_EXIT.y, 24, 24, 6, hot ? C_SEL : C_BG);
        info_icon(R_EXIT.x, R_EXIT.y, hot ? C_INK : C_INK2);
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
        /* THE EXAMPLES ARE PART OF THE EMPTY STATE, so they are inside the block that gets
         * centred. Centring heading+field alone and then appending them below put the last row
         * 4 px past the pane and the fit guard silently dropped ALL THREE -- the same silent
         * truncation this file already fixed twice, in the session list and the search sheet. */
        int lh  = gfx_font_h(F_SM) + 5;
        /* ONLY WHILE THE BOX IS EMPTY. They are a prompt to start, not furniture -- and the
         * geometry requires it: the composer grows UPWARD with its text and its bottom edge must
         * not move, which holds because `bot` shrinks by exactly what the block grows. Adding a
         * constant to that block pushed the layout into its `cy0 < top + 6` clamp at four lines,
         * the cancellation stopped being exact, and the bottom edge started moving. test_exit
         * caught it -- the assertion is older than this feature and names the reason. */
        /* A135. THE "TRY ONE" EXAMPLES ARE GONE. Reported from the device: "I keep accidentally
         * clicking it. It's ugly." Accidental activation is the part that matters -- the list sat
         * directly under the composer, so a click that missed the box low ran a question the
         * student never asked for. The key row along the bottom (menu, symbols, catalog,
         * formulas) already tells them what the app can do, which was this block's other job. */
        int blk = hh + gap + compose_field_h(w);
        int cy0 = top + (bot - top - blk) / 2;
        if (cy0 < top + 6) cy0 = top + 6;

        /* Heading then field, nothing between -- `#empty` is `h1` + composer and no subtitle. The
         * "Ask in your own words." line was mine, not the design's. */
        const char *h1 = "What can I work out?";
        gfx_text(x0 + (w - gfx_text_w(h1, F_BIG)) / 2, cy0, h1, F_BIG, C_INK, C_BG);
        draw_composer(x0, w, cy0 + hh + gap);
        EMPTY_COMPOSER = 1;

        (void)lh;
    } else {
        app_chat *c = &CHATS[CUR];
        int lh0 = gfx_font_h(F_UI) + 2, total = 6;
        for (int i = 0; i < c->nturns; i++) {
            app_turn *t = &c->turn[i];
            total += gfx_text_wrap(0, 0, t->q, F_UI, C_INK, C_BUBBLE, qbubble_textw(pw), lh0, 0) * lh0 + 8 + QACT_H;
            if (BUSY && i == c->nturns - 1 && STATUS[0])
                total += draw_status(0, 0, pw, STATUS, STATUS_MONO, 1, 0);
            else if (t->done && t->sum[0])
                total += draw_status(0, 0, pw, t->sum, 0, 0, 0);
            total += t->a[0] ? draw_answer(0, 0, pw, t->a, 0) + 10 + ACT_H : 20;
        }
        clamp_scroll(total, bot - top);
        int y = top + 6 - SCROLL;
        for (int i = 0; i < c->nturns; i++) {
            app_turn *t = &c->turn[i];
            /* The question bubble SHRINKS TO ITS TEXT and sits flush right.
             *
             * It used to be drawn at a fixed `pw - 16` whatever the question said, so "hi" got the
             * same slab as a three-line problem and the text sat against its left edge with a hand
             * of empty bubble beside it. The comment here claimed "right-aligned" the whole time,
             * which was true of nothing in the code beneath it.
             *
             * maxtw is still the cap, so the wrap and the line count are unchanged; the only new
             * number is how much of that width the text actually uses. Measured and drawn at the
             * SAME maxtw deliberately -- measuring at one width and drawing at another is the
             * bubble defect that took a 60-case sweep to find, and it would come straight back if
             * this wrapped at the shrunken width instead. */
            int lh = gfx_font_h(F_UI) + 2;
            int maxtw = qbubble_textw(pw);
            int lines = gfx_text_wrap(0, 0, t->q, F_UI, C_INK, C_BUBBLE, maxtw, lh, 0);
            int tw = gfx_text_wrap_w(t->q, F_UI, maxtw);
            if (tw > maxtw) tw = maxtw;                  /* belt: never wider than the cap */
            int bh = lines * lh + 8;
            int bw = tw + 16;                            /* 8px of padding on each side */
            int bx = px0 + pw - bw;                      /* flush against the pane's right edge */
            gfx_rrect(bx, y, bw, bh, 7, C_BUBBLE);
            if (SEL_ALL)
                gfx_text_wrap_sel(bx + 8, y + 4, t->q, F_UI, C_INK, C_BUBBLE, maxtw, lh,
                                  0, (int)strlen(t->q), C_SELTEXT);
            else
                gfx_text_wrap(bx + 8, y + 4, t->q, F_UI, C_INK, C_BUBBLE, maxtw, lh, 1);
            /* YOUR OWN TEXT GETS A COPY CONTROL TOO. The action row only ever appeared under
             * answers, so the one message you might want to reuse verbatim -- a question worth
             * asking again in another session -- was the one you could not lift. Right-aligned
             * under the bubble, following its edge rather than the pane's, because the bubble no
             * longer spans the pane. */
            R_QACT[i] = (gfx_rect){ bx + bw - ACT_SZ, y + bh + 1, ACT_SZ, ACT_SZ };
            {   gfx_rect qb = { bx, y, bw, bh + QACT_H };
                if (HOVER && inside(qb, MX, MY)) {
                    int on = inside(R_QACT[i], MX, MY);
                    if (on) gfx_rrect(R_QACT[i].x, R_QACT[i].y, ACT_SZ, ACT_SZ, 3, C_TRASH_HOT);
                    blit(R_QACT[i].x + 2, R_QACT[i].y + 2, IC_COPY, 10,
                         on ? C_INK : C_INK3, C_BG);
                }
            }
            y += bh + QACT_H;
            /* The status line sits between the question and the answer: live while this turn is
             * generating, and the permanent one-line summary once it is done. */
            if (BUSY && i == c->nturns - 1 && STATUS[0])
                y += draw_status(px0, y, pw, STATUS, STATUS_MONO, 1, 1);
            else if (t->done && t->sum[0])
                y += draw_status(px0, y, pw, t->sum, 0, 0, 1);

            if (t->a[0]) {
                /* The selected span is highlighted by the SAME call that draws the rest, so a
                 * highlight can never land on text that is not there. */
                int ah = draw_answer_ex(px0, y, pw, t->a, 1,
                                        SEL_ALL ? -2 : (i == SEL_TURN ? SEL_SPAN : -1),
                                        SEL_A, SEL_B, 0, -1, 0, 0);
                /* Recorded for the drag probe and for ctrl+c with no selection. */
                R_ANS[i] = (gfx_rect){ px0, y, pw, ah };
                int ay = y + ah + 1;
                /* The whole answer plus its reserved row is the hover target, not the icons alone.
                 * Requiring the pointer to already be on a 14px control before that control
                 * appears is a chicken-and-egg the reader cannot win. */
                gfx_rect band = { px0, y, pw, ah + ACT_H };
                int show = HOVER && inside(band, MX, MY);
                for (int k = 0; k < 3; k++)
                    R_ACT[i][k] = (gfx_rect){ px0 + k * (ACT_SZ + 4), ay, ACT_SZ, ACT_SZ };
                if (i + 1 > NACT_ROWS) NACT_ROWS = i + 1;
                /* A rating stays lit once given, hover or not: it is the answer to "what did I say
                 * about this one", which is not a question about where the pointer is. */
                for (int k = 0; k < 3; k++) {
                    int rated = (k == 1 && t->rating > 0) || (k == 2 && t->rating < 0);
                    if (!show && !rated) continue;
                    gfx_rect r = R_ACT[i][k];
                    int on = HOVER && inside(r, MX, MY);
                    if (on) gfx_rrect(r.x, r.y, r.w, r.h, 3, C_TRASH_HOT);
                    uint16_t ink = rated ? C_INK : (on ? C_INK : C_INK3);
                    blit(r.x + 2, r.y + 2, k == 0 ? IC_COPY : k == 1 ? IC_UP : IC_DOWN,
                         k == 0 ? 10 : 9, ink, C_BG);
                }
                y += ah + 10 + ACT_H;
            }
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

    /* The toast, last, so it sits over everything. Copy and paste leave no other trace, and a
     * control that looks identical whether it worked or not is the silence problem again. */
    if (TOAST[0]) {
        int tw = gfx_text_w(TOAST, F_SM) + 16, th = gfx_font_h(F_SM) + 8;
        int tx = x0 + (w - tw) / 2, ty = GFX_H - dock - th - 6;
        if (ty < TOP_H + 4) ty = TOP_H + 4;
        gfx_rrect(tx, ty, tw, th, 5, C_SHEET);
        gfx_rrect_outline(tx, ty, tw, th, 5, C_LINE);
        gfx_text(tx + 8, ty + 4, TOAST, F_SM, C_INK, C_SHEET);
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

/* SETTINGS. The gear's contents: what the keys do, and how the theme is chosen.
 *
 * The shortcut key lives here because a chord nobody can discover is not a feature -- ctrl+B was
 * unfindable by any means other than being told. The theme moved out of the icon band for the
 * opposite reason: it is set once and then never touched, so it was spending a quarter of a
 * permanent row on a decision made on first run. */
/* One number, used by the table below AND by the height that has to contain it. Two places knowing
 * the row count is how the sheet overflowed three times. */
#define SHORTCUT_N 10
#define SHORTCUT_COLS 2
/* Rounded UP, so an odd count still reserves the line its last entry sits on. */
#define SHORTCUT_ROWS ((SHORTCUT_N + SHORTCUT_COLS - 1) / SHORTCUT_COLS)

static void draw_settings(void) {
    /* THE HEIGHT IS COMPUTED, not written down. It was hardcoded at 132, then 150, then 174, and
     * outgrew every one of them: each time the last row drew through the bottom edge and onto the
     * transcript, and each time the fix was to measure the render and pick a bigger number. Adding
     * ctrl+c and ctrl+v would have been the fourth.
     *
     * A constant that has to be re-derived whenever the content changes is a constant that will be
     * wrong again. This sums the same row heights the drawing code below uses, so a new row cannot
     * overflow it -- the sheet grows instead. */
    const int lh_sm = gfx_font_h(F_SM);
    const int H = 8                          /* top inset                        */
               + gfx_font_h(F_UIB) + 6       /* title                            */
               + lh_sm + 3 + 16 + 8          /* Appearance label, chips, gap     */
               + lh_sm + 8                   /* time zone row                    */
               + lh_sm + 2                   /* Shortcuts label                  */
               + SHORTCUT_ROWS * (lh_sm + 2) /* the key rows, two to a line      */
               + 8;                          /* bottom inset                     */
    /* Y follows the height rather than leading it. 30 is where it belongs -- just under the top
     * bar -- but a computed height can now exceed what is left below that line, and at exactly
     * 240 the bottom border was clipped by the screen edge with a margin of 0. It floats up only
     * as far as it has to, and only when it has to. */
    const int W = 232, X = (GFX_W - W) / 2;
    int Y = 30;
    if (Y + H > GFX_H - 6) Y = GFX_H - 6 - H;
    if (Y < 4) Y = 4;
    gfx_dim(C_SCRIM, 28);
    gfx_rrect(X - 1, Y - 1, W + 2, H + 2, 9, C_LINE);
    gfx_rrect(X, Y, W, H, 8, C_SHEET);

    int y = Y + 8;
    gfx_text(X + 10, y, "Settings", F_UIB, C_INK, C_SHEET);
    y += gfx_font_h(F_UIB) + 6;

    /* the theme row: three states, the current one filled */
    gfx_text(X + 10, y, "Appearance", F_SM, C_INK3, C_SHEET);
    y += gfx_font_h(F_SM) + 3;
    {   static const char *NAME[3] = { "System", "Light", "Dark" };
        int bx = X + 10;
        R_SET_THEME = (gfx_rect){ bx, y, W - 20, 16 };
        for (int i = 0; i < 3; i++) {
            int w = gfx_text_w(NAME[i], F_SM) + 12;
            int on = (app_theme() == i);
            gfx_rrect(bx, y, w, 16, 4, on ? C_SEL : C_SHEET);
            if (!on) gfx_rrect_outline(bx, y, w, 16, 4, C_LINE);
            gfx_text(bx + 6, y + 1, NAME[i], F_SM, on ? C_INK : C_INK2,
                     on ? C_SEL : C_SHEET);
            bx += w + 6;
        }
        y += 22;
    }

    /* The offset, shown only when System is the choice, because it changes nothing otherwise and
     * a control that does nothing is worse than no control. */
    if (app_theme() == TH_AUTO) {
        int h = app_clock_hour();
        gfx_text(X + 10, y, "Time zone", F_SM, C_INK3, C_SHEET);
        int bx = X + 10 + gfx_text_w("Time zone ", F_SM);
        R_SET_TZM = (gfx_rect){ bx, y - 1, 15, 15 };
        R_SET_TZP = (gfx_rect){ bx + 19, y - 1, 15, 15 };
        gfx_rrect_outline(R_SET_TZM.x, R_SET_TZM.y, 15, 15, 4, C_LINE);
        gfx_rrect_outline(R_SET_TZP.x, R_SET_TZP.y, 15, 15, 4, C_LINE);
        gfx_fill(R_SET_TZM.x + 4, R_SET_TZM.y + 7, 7, 1, C_INK2);
        gfx_fill(R_SET_TZP.x + 4, R_SET_TZP.y + 7, 7, 1, C_INK2);
        gfx_fill(R_SET_TZP.x + 7, R_SET_TZP.y + 4, 1, 7, C_INK2);
        gfx_text(bx + 40, y, app_tz_name(), F_SM, C_INK2, C_SHEET);
        /* what it resolves to, so the setting can be checked rather than guessed at */
        if (h >= 0) {
            char now[32];
            int lh = (h + app_tz()) % 24; if (lh < 0) lh += 24;
            snprintf(now, sizeof now, "%02d:00 local", lh);
            gfx_text(bx + 92, y, now, F_SM, C_INK3, C_SHEET);
        } else {
            /* "no clock" rather than "clock unreadable": the long form is 85px starting at
             * bx+92, which runs 12px past the sheet's right edge. */
            gfx_text(bx + 92, y, "no clock", F_SM, C_INK3, C_SHEET);
        }
        y += 20;
    }

    /* the shortcut key */
    gfx_text(X + 10, y, "Shortcuts", F_SM, C_INK3, C_SHEET);
    y += gfx_font_h(F_SM) + 2;
    {   /* LOWERCASE letters. "ctrl N" reads as though the shift is part of it, which would be a
         * different chord entirely; both cases work but only one should be printed.
         * F_SM rather than F_XS: 9px is fine for a disclaimer nobody reads twice and too small for
         * a reference somebody is squinting at to learn the app. */
        static const char *K[SHORTCUT_N][2] = {
            { "ctrl n", "New chat" },   { "ctrl s", "Search" },
            { "ctrl b", "Side panel" }, { "ctrl a", "Select all" },
            { "ctrl c", "Copy" },       { "ctrl v", "Paste" },
            /* A127. THE TWO KEYS NOBODY COULD FIND. The symbol palette has been on the menu key
             * since A?? and the formula library is now on the catalog key, and NEITHER was written
             * down anywhere in the app. Reported from the device: the catalog key ("the book icon")
             * appeared to do nothing, and the palette was found only by accident -- "okay, I see
             * the table now in menu". A key binding that is not printed is a feature that does not
             * exist for anyone who did not write it. */
            { "menu", "Symbols" },      { "catalog", "Formulas" },
            { "ctrl esc", "Quit" },     { "esc", "Back" },
        };
        /* TWO COLUMNS. Eight rows in one column made the sheet 232px tall on a 240px screen: it
         * hit its own top clamp and sat squeezed against both edges. The pairs are narrow and the
         * sheet is 232 wide, so the width was there all along. */
        for (int i = 0; i < SHORTCUT_N; i++) {
            int cx = X + 10 + (i % SHORTCUT_COLS) * ((W - 20) / SHORTCUT_COLS);
            gfx_text(cx,      y, K[i][0], F_SM, C_INK2, C_SHEET);
            gfx_text(cx + 46, y, K[i][1], F_SM, C_INK3, C_SHEET);
            if (i % SHORTCUT_COLS == SHORTCUT_COLS - 1) y += gfx_font_h(F_SM) + 2;
        }
        if (SHORTCUT_N % SHORTCUT_COLS) y += gfx_font_h(F_SM) + 2;
    }

    /* NO QUIT BUTTON. It sat inside a sheet people open to read the shortcut key, one slip from
     * ending the session, and it is not needed: ctrl+esc is listed two lines above. A destructive
     * control does not belong in a reference panel. */
}

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
        /* Where the text was actually laid out, kept so a CLICK can be turned back into a byte
         * offset by gfx_text_wrap_hit with the same origin and width the glyphs used. Recomputing
         * it at the click site would be a second copy of the layout rule. */
        TEXT_X = R_FIELD.x + 9; TEXT_Y = cy + 5 - skip * COMPOSE_LH; TEXT_W = tw;
        /* Clipped to the TEXT band, not to the field. Clipping to the field left the bottom 5px
         * of the line above the first visible one showing -- a row of glyph-tops with no line
         * under them, which reads as a rendering fault rather than as scrolled text. */
        gfx_clip(R_FIELD.x, cy + 5, R_FIELD.w, shown * COMPOSE_LH);
        if (COMPOSE_SEL) {
            gfx_text_wrap_sel(R_FIELD.x + 9, cy + 5 - skip * COMPOSE_LH, COMPOSE, F_UI, C_INK,
                              C_FIELD, tw, COMPOSE_LH, 0, COMPOSE_N, C_SELTEXT);
            last_w = 0;
        } else {
            gfx_text_wrap_ex(R_FIELD.x + 9, cy + 5 - skip * COMPOSE_LH, COMPOSE, F_UI, C_INK,
                             C_FIELD, tw, COMPOSE_LH, 1, &last_w);
        }
        gfx_clip_reset();
        /* THE CARET SITS AT COMPOSE_C, not at the end of the text.
         *
         * It used to be drawn at `last_w`, the width of the whole string, which was correct only
         * because the field could not be edited anywhere but the end. With left/right it would
         * have pointed at the wrong place on every keystroke.
         *
         * The position is measured the same way the text is laid out -- by WRAPPING THE PREFIX
         * COMPOSE[0, COMPOSE_C) with drawing off. Its last-line width is the caret's x and its
         * line count is the caret's row, so the caret cannot disagree with the glyphs: both come
         * from gfx_text_wrap_ex on the same width. */
        int caret_w = last_w, caret_line = total;
        if (COMPOSE_C < COMPOSE_N) {
            static char pre[sizeof COMPOSE];
            int pn = COMPOSE_C < (int)sizeof pre - 1 ? COMPOSE_C : (int)sizeof pre - 1;
            memcpy(pre, COMPOSE, (size_t)pn); pre[pn] = 0;
            caret_line = gfx_text_wrap_ex(0, 0, pre, F_UI, C_INK, C_FIELD, tw, COMPOSE_LH,
                                          0, &caret_w);
            if (caret_line < 1) caret_line = 1;
        }
        int caret_row = caret_line - 1 - skip;
        if (caret_row < 0) caret_row = 0;
        if (caret_row > shown - 1) caret_row = shown - 1;
        int cxx = R_FIELD.x + 9 + caret_w + 1, cyy = cy + 6 + caret_row * COMPOSE_LH;
        /* Blinks on the app clock, half a second on and half off. A steady bar reads as a piece
         * of the layout; a blinking one reads as the insertion point. */
        /* No caret while everything is selected. A blinking insertion point next to a full
         * highlight says two contradictory things about where the next keystroke lands. */
        if (!COMPOSE_SEL && caret_w < tw - 2 && (NOW_MS / 500u) % 2u == 0u)
            gfx_vline(cxx, cyy, 12, C_INK);
    } else if (FIELD_FOCUS) {
        /* Focused and empty: a blinking caret and no placeholder. The caret is the thing that says
         * "type here", so the hint beside it would be saying the same thing twice, and the two
         * would overlap at the same x. */
        if ((NOW_MS / 500u) % 2u == 0u) gfx_vline(R_FIELD.x + 10, cy + 6, 12, C_INK);
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
    /* A toast has to be redrawn when it EXPIRES, not only when it appears -- otherwise it hangs on
     * screen until something else happens to trigger a frame, which on an idle calculator can be
     * a long time. */
    if (TOAST[0] && ms >= TOAST_UNTIL) { TOAST[0] = 0; moved = 1; }
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

/* ---- the relation picker ------------------------------------------------------------------
 * Decision E, finally wired. app_request has always taken a `rid` and NOTHING ever produced one:
 * both call sites passed 0 and the function ignored the parameter, so every question the device
 * could ask was assembled against record 0 with no givens. E exists because retrieval is weak --
 * 8.0% on the clean surface, reproduced on device at 9.5% over 200 labelled questions -- so the
 * student names the relation and the runtime states fit:high by construction.
 *
 * The STATE MACHINE is in pickui.c and tested on the host (36 assertions, 12/12 controls). This
 * file only draws it and routes keys, which is the split that lets the navigation be checked at
 * all: esc-goes-up-one-level is decidable without a calculator, and how it looks is not. */
static pk_state PK;
static int  PICK_ON;
static int  LIB_MODE;   /* A127: the picker opened as a reference library, not as a question */
static char PENDQ[sizeof COMPOSE];        /* the question, held while the relation is chosen */
static gfx_rect R_PROW[PK_ROWS];

static void picker_send(const char *rid) {
    PICK_ON = 0;
    app_request(PENDQ, rid);
    PENDQ[0] = 0;
}

static void open_picker(void) {
    const ns_store2 *st = app_store();
    /* NO STORE IS NOT A REASON TO SWALLOW THE QUESTION. Form C is exactly the right prompt when
     * no relation can be offered, and it is what the student would have got by pressing esc. */
    if (!st) { snprintf(PENDQ, sizeof PENDQ, "%s", COMPOSE); compose_clear(); picker_send(0); return; }
    snprintf(PENDQ, sizeof PENDQ, "%s", COMPOSE);
    compose_clear();
    /* A88. SKIP THE PICKER WHEN THE QUESTION IS ALREADY ANSWERED BY ONE RECORD.
     *
     * "what is hookes law" does not need a shortlist: the record's name IS the question. Until now
     * every question opened the picker, including all six from the device transcript, so the
     * demo's shortest path was three screens.
     *
     * THE FALLBACK IS THE PICKER, NEVER A REFUSAL, and that is what makes this safe to ship
     * without the device. A false negative costs exactly one screen -- today's behaviour for every
     * question -- so this can only reduce screens, never lose an answer. A false positive shows a
     * record the student did not choose, which is bounded by measurement: 2.2% of 2,000 certified
     * out-of-scope questions and 6.8% of word problems clear the bar, and those word problems
     * retrieve at 71.4% against 28.1% for the ones that fall through.
     *
     * Deliberately NOT ask_pick's score. ask_pick cannot say "no match" at all -- it returns
     * record 0, Hooke's law -- and a cut point on its score refuses 49.2% of out-of-scope while
     * keeping 68.0% of in-scope. See ask_confident for the measurement that chose this instead. */
    {
        static ns_ask aq;
        ask_parse(PENDQ, &aq);
        int idx = -1;
        if (ask_confident(st, PENDQ, &aq.in, &idx) && idx >= 0 && st->rec[idx].rid) {
            picker_send(st->rec[idx].rid);
            return;
        }
    }
    /* A125. THE PICKER NEVER APPEARS IN THE ANSWER PATH. The fallback is a REFUSAL, not a prompt.
     *
     * Asked for twice, in these words: "It should never present the option for someone to select
     * something, delete it, just delete it. It should either say an answer or it doesn't know."
     *
     * And it was not only unwanted, it was actively harmful. The student cannot know which record
     * is right -- that is the retrieval problem, handed to them -- and a wrong pick produces a
     * confident answer about the wrong relation. Reported from the device: picking from the
     * shortlist after a rearrangement gave "p equals f times v" for a question about m = F/a.
     *
     * WHAT MAKES THE REFUSAL SAFE IS A124, NOT OPTIMISM. Before it, calculus and rearrangement
     * questions were confident 0% of the time, so deleting the picker would have turned every one
     * of them into "I cannot answer that". With the relation-carried rule they are confident, and
     * the fallback now catches what it should: out-of-scope questions and word problems.
     *
     *     rule                     in-scope   out-of-scope false positives
     *     coverage only (before)      73.3%          2.2%
     *     + relation carried          93.3%          1.6%
     *
     * picker_send(0) is Form C -- the prompt with no record, which the corpus trains as a refusal
     * and which measured 100.0% refused and 0.0% confident answers on 88 questions the store
     * cannot serve. It is the same path esc always took. */
    picker_send(0);
}

static void draw_symbols(void) {
    const int cw = 58, ch = 22, cols = SYM_COLS;
    const int n = pal_n();
    const int rows = (n + cols - 1) / cols;
    const int W = cols * cw + 16, H = rows * ch + 56;
    const int X = (GFX_W - W) / 2, Y = (GFX_H - H) / 2;
    gfx_fill(0, 0, GFX_W, GFX_H, C_SCRIM);
    gfx_rrect(X, Y, W, H, 8, C_SHEET);
    R_SYMSHEET = (gfx_rect){ X, Y, W, H };
    gfx_text(X + 10, Y + 5, "INSERT", F_XS, C_INK3, C_SHEET);

    /* Category tabs. SYM_SEL == -1 parks the cursor here, so UP from the first row reaches them
     * and DOWN returns: the grid and the tabs are one focus chain rather than two modes. */
    int tx = X + 8;
    for (int c = 0; c < CAT_N; c++) {
        int tw = gfx_text_w(PAL_CAT[c].name, F_XS) + 8;
        gfx_rect b = { tx, Y + 16, tw, 13 };
        R_CAT[c] = b;
        int on = (c == SYM_CAT);
        if (on) gfx_rrect(b.x, b.y, b.w, b.h, 3, SYM_SEL < 0 ? C_SEL : C_BUBBLE);
        gfx_text(b.x + 4, b.y + 2, PAL_CAT[c].name, F_XS,
                 on ? C_INK : C_INK3, on ? (SYM_SEL < 0 ? C_SEL : C_BUBBLE) : C_SHEET);
        tx += tw + 3;
    }
    gfx_fill(X + 8, Y + 32, W - 16, 1, C_LINE);

    for (int i = 0; i < SYM_MAX; i++) { R_SYM[i].w = 0; }
    for (int i = 0; i < n; i++) {
        int r = i / cols, c = i % cols;
        gfx_rect b = { X + 8 + c * cw, Y + 37 + r * ch, cw - 4, ch - 3 };
        R_SYM[i] = b;
        int sel = (i == SYM_SEL);
        if (sel) gfx_rrect(b.x, b.y, b.w, b.h, 4, C_SEL);
        int tw = gfx_text_w(pal_lab(i), F_UI);
        gfx_text(b.x + (b.w - tw) / 2, b.y + 3, pal_lab(i), F_UI, C_INK, sel ? C_SEL : C_SHEET);
    }
    gfx_text(X + 10, Y + H - 13, "enter insert   tab category   esc close", F_XS, C_INK3, C_SHEET);
}

static void draw_picker(void) {
    const ns_store2 *st = app_store();
    if (!st) return;
    const int X = 6, Y = 4, W = GFX_W - 12, H = GFX_H - 8;
    const int rowh = 13;
    gfx_fill(0, 0, GFX_W, GFX_H, C_SCRIM);
    gfx_rrect(X, Y, W, H, 8, C_SHEET);

    char head[72];
    int rows, n, top;
    if (PK.level == PK_FAMILY) {
        /* A127. THE HEADING SAYS WHAT THIS IS. "WHAT ARE YOU SOLVING FOR?" was the app asking the
         * student to do retrieval; the library is the student looking something up. Same rows,
         * opposite direction, and the wording is the only thing that says which. */
        snprintf(head, sizeof head, LIB_MODE ? "FORMULA LIBRARY" : "WHAT ARE YOU SOLVING FOR?");
        /* MODEL ROWS, NOT FAMILIES. The family level draws the suggestion rows ABOVE the families
         * and pk_row_family()/pk_row_is_sug() index over both, which is also what clamp() scrolls
         * over. Using the family count alone made this disagree with the scroller in two visible
         * ways: the footer said "2 more below" with SEVEN families off-screen, and at the bottom of
         * the list `rows` came out five short, so the last five families could be scrolled to and
         * not drawn. Same hidden-data class as the session list and the search sheet -- silence
         * about what was dropped is the bug, not the limit. */
        n = PK.nsug + PK.nfam;
    } else if (PK.nhit == 0) {
        snprintf(head, sizeof head, "No relation matches \"%s\"", PK.q);
        n = 0;
    } else if (PK.fam >= 0) {
        snprintf(head, sizeof head, "%s", ns_family_name(PK.fam));
        n = PK.nhit;
    } else {
        snprintf(head, sizeof head, "All relations  \"%s\"", PK.q);
        n = PK.nhit;
    }
    gfx_text_ellipsis(X + 8, Y + 5, head, F_UIB, C_INK, C_SHEET, W - 16);
    gfx_fill(X + 8, Y + 19, W - 16, 1, C_LINE);

    /* THE QUESTION STAYS ON SCREEN. Choosing a relation for a question you can no longer see is
     * the sort of modal that gets answered wrong, and PENDQ is the only copy while this is up. */
    gfx_text_ellipsis(X + 8, Y + 22, PENDQ, F_XS, C_INK3, C_SHEET, W - 16);

    top = Y + 36;
    int page = PK.rows > 0 ? PK.rows : PK_ROWS;
    rows = (n - PK.scroll) < page ? (n - PK.scroll) : page;
    for (int i = 0; i < PK_ROWS; i++) R_PROW[i] = (gfx_rect){0, 0, 0, 0};

    if (n == 0) {
        /* THE EMPTY SEARCH IS A SCREEN CARRYING THE BASE RATE, not an error. 44% of real textbook
         * questions have no matching relation; a student told that moves on, a student shown an
         * error retypes. The number is measured on the clean surface. */
        gfx_text(X + 10, top,          "The store has 164 relations and none of", F_SM, C_INK3, C_SHEET);
        gfx_text(X + 10, top + rowh,   "them fit this. That happens for about 4", F_SM, C_INK3, C_SHEET);
        gfx_text(X + 10, top + 2*rowh, "questions in 10.", F_SM, C_INK3, C_SHEET);
    } else {
        int ry = top;
        for (int i = 0; i < rows; i++) {
            int r = PK.scroll + i, selrow = (r == PK.sel);
            /* SECTION HEADERS. Drawn only when their boundary is inside the visible window, and
             * they cost a row's worth of page size (pk_open reserves it), so the selected row can
             * never be pushed off the bottom by one appearing. */
            if (PK.level == PK_FAMILY && PK.nsug) {
                if (r == 0) {
                    /* LABELLED AS A GUESS, DELIBERATELY. The right relation is in these five about
                     * half the time; a section that read as authoritative would spend confidence
                     * the ranker has not earned, and a student cannot audit a ranker -- they can
                     * read a formula. So the instruction is to check the formula, and the formula
                     * is on the row. */
                    gfx_text(X + 12, ry, "SUGGESTIONS  -  check the formula", F_XS, C_INK3, C_SHEET);
                    ry += 10;
                } else if (r == PK.nsug) {
                    gfx_text(X + 12, ry, "OR BROWSE BY QUANTITY", F_XS, C_INK3, C_SHEET);
                    ry += 10;
                }
            }
            gfx_rect rr = (gfx_rect){X + 6, ry - 1, W - 12, rowh};
            R_PROW[i] = rr;
            if (selrow) gfx_rrect(rr.x, rr.y, rr.w, rr.h, 3, C_SEL);
            uint16_t bg = selrow ? C_SEL : C_SHEET;
            if (PK.level == PK_FAMILY && pk_row_is_sug(&PK, r)) {
                /* A DIFFERENT SHAPE FROM A FAMILY ROW, at a glance: indented under its label, and
                 * carrying the FORMULA on the right where a family carries a count. The formula is
                 * the thing that tells a student whether the guess is right. */
                const ns_rec2 *rec = &st->rec[PK.sug[r]];
                int fw = gfx_text_w(rec->formula, F_XS);
                if (fw > (W - 24) / 2) fw = (W - 24) / 2;
                gfx_text_ellipsis(X + 20, ry, rec->name ? rec->name : rec->formula,
                                  F_SM, C_INK, bg, W - 40 - fw);
                gfx_text_ellipsis(X + W - 14 - fw, ry + 1, rec->formula, F_XS, C_INK3, bg, fw);
            } else if (PK.level == PK_FAMILY) {
                int f = pk_row_family(&PK, r);
                char cnt[8]; snprintf(cnt, sizeof cnt, "%d", PK.fams[f].count);
                int cw = gfx_text_w(cnt, F_SM);
                gfx_text_ellipsis(X + 12, ry, PK.fams[f].name, F_SM, C_INK, bg, W - 30 - cw);
                gfx_text(X + W - 12 - cw, ry, cnt, F_SM, C_INK3, bg);
            } else {
                const ns_rec2 *rec = &st->rec[PK.hit[r]];
                gfx_text_ellipsis(X + 12, ry, rec->name ? rec->name : rec->formula,
                                  F_SM, C_INK, bg, W - 24);
            }
            ry += rowh;
        }
        if (n > PK.scroll + rows) {
            char more[40];
            snprintf(more, sizeof more, "%d more below", n - PK.scroll - rows);
            gfx_text(X + 12, ry + 1, more, F_XS, C_INK3, C_SHEET);
        }
    }

    /* The key legend, per level. Naming the exits is the whole reason esc is safe here. */
    const char *legend =
        (PK.level == PK_FAMILY && pk_row_is_sug(&PK, PK.sel))
                                ? "enter pick   tab browse   type search   esc ask" :
        (PK.level == PK_FAMILY && PK.nsug)
                                ? "enter open   tab suggest  type search   esc ask" :
        /* A127. THE LIBRARY SAYS WHAT IT DOES. "esc ask anyway" and "enter pick" are the picker's
         * words -- the app asking which relation the student meant. In the library the student is
         * looking something up and ENTER puts the formula in the box, so the legend says INSERT.
         * A legend that describes the other mode is how a screen teaches the wrong thing. */
        (PK.level == PK_FAMILY) ? (LIB_MODE ? "enter open   type search   esc close"
                                            : "enter open   type search   esc ask anyway") :
        (n == 0)                ? (LIB_MODE ? "bksp edit    esc back"
                                            : "bksp edit    esc browse    a ask anyway") :
                                  (LIB_MODE ? "enter insert   type filter   esc back"
                                            : "enter pick   type filter   esc back");
    gfx_fill(X + 8, Y + H - 17, W - 16, 1, C_LINE);
    gfx_text(X + 10, Y + H - 14, legend, F_XS, C_INK3, C_SHEET);
}

void app_draw(void) {
    gfx_clear(C_BG);
    if (SIDEBAR) draw_sidebar();
    draw_main();
    if (SEARCH_ON) draw_search();
    if (SETTINGS_ON) draw_settings();
    if (PICK_ON) draw_picker();
    if (SYM_ON) draw_symbols();
    draw_cursor();          /* last, so nothing occludes it */
    gfx_present();
}

/* ---- input ------------------------------------------------------------------------------------ */
/* What the New chat control and ctrl+N both do. It does NOT create a session -- new_chat() runs on
 * the first send -- it returns to the empty screen and clears the box. Named because the button
 * and the chord were about to hold two copies of that, and the chord's copy called new_chat(),
 * which would have left an untitled empty session in the list on every press. */
static void start_new_chat(void) { CUR = -1; SCROLL = 0; compose_clear(); }
static void open_search(void)    { SEARCH_ON = 1; SQ_N = 0; SQ[0] = 0; SSEL = 0; run_search(); }

/* A127. THE FORMULA LIBRARY. The picker's browse machinery -- families, filter, scroll -- was left
 * with NO CALLER when A125 removed it from the answer path, and a student pressing the catalog key
 * found nothing. Both halves of that are fixed here: the code is reachable again and it is
 * reachable DELIBERATELY, as a reference the student opens, never as a question the app asks.
 *
 * PICKING INSERTS, IT DOES NOT SEND. That is the whole difference from the old picker and it is
 * why this is safe: the app is not asking "which relation did you mean" (the retrieval problem,
 * handed to the student, which produced "p equals f times v" for a question about m = F/a). It is
 * handing them the formula to look at or to ask about. */
static void open_library(void) {
    const ns_store2 *st = app_store();
    if (!st) return;
    pk_open(&PK, st, "");        /* no question: the family list, not a shortlist */
    PICK_ON = 1; LIB_MODE = 1;
}

void app_event(const in_event *e) {
    if (e->kind == IN_MOVE)  {
        MX = e->x; MY = e->y; HOVER = e->hover;
        /* A129. THE POINTER DRIVES THE MODAL LISTS, because on this hardware it is the only thing
         * that reliably can.
         *
         * THE CX II HAS NO ARROW KEYS. The touchpad IS the arrow ring, and a light press on its
         * edge is a CONTACT, not a keypress -- so it moves the cursor and K_UP/K_DOWN never fire.
         * The picker and the palette were built around those keys, which is why both were reported
         * as "impossible and very challenging to scroll and then to click stuff".
         *
         * Two behaviours, and together they make a list usable with nothing but the pad:
         *   HOVER SELECTS -- the highlight follows the finger, so what a click will do is visible
         *     before the click, which is also what makes clicking feel accurate.
         *   THE EDGES SCROLL -- moving past the last drawn row scrolls by one, and past the first
         *     scrolls back. Without this a list longer than the sheet is simply unreachable, which
         *     is the same hidden-data failure as a list that stops drawing.
         *
         * Keys still work exactly as before; this is additive. */
        if (PICK_ON) {
            int rows = 0;
            for (int i = 0; i < PK_ROWS; i++) if (R_PROW[i].w) rows++;
            int hit_row = -1;
            for (int i = 0; i < rows; i++)
                if (inside(R_PROW[i], MX, MY)) { hit_row = i; break; }
            if (hit_row >= 0) {
                PK.sel = PK.scroll + hit_row;
            } else if (rows > 0) {
                /* Past the ends of the drawn list: scroll rather than do nothing. */
                const gfx_rect *first = &R_PROW[0], *last = &R_PROW[rows - 1];
                int n = (PK.level == PK_FAMILY) ? PK.nsug + PK.nfam : PK.nhit;
                if (MY > last->y + last->h && MX >= last->x && MX <= last->x + last->w) {
                    if (PK.scroll + rows < n) { PK.scroll++; if (PK.sel < PK.scroll) PK.sel = PK.scroll; }
                } else if (MY < first->y && MX >= first->x && MX <= first->x + first->w) {
                    if (PK.scroll > 0) { PK.scroll--; if (PK.sel >= PK.scroll + rows) PK.sel = PK.scroll + rows - 1; }
                }
            }
        } else if (SYM_ON) {
            for (int i = 0; i < pal_n(); i++)
                if (R_SYM[i].w && inside(R_SYM[i], MX, MY)) { SYM_SEL = i; break; }
        }
        /* Press starts a selection, press-and-move extends it, release leaves it standing. The
         * selection outlives the drag deliberately: ctrl+c comes after the finger lifts. */
        if (e->pressed && !DRAGGING)      { DRAGGING = 1; sel_begin(MX, MY); }
        else if (e->pressed && DRAGGING)  { sel_extend(MX, MY); }
        else if (!e->pressed && DRAGGING) { DRAGGING = 0; }
        return;
    }
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
        /* A plain tap drops the selection, as it does everywhere else. A press-DRAG never reaches
         * here: the pointer layer reports its release as a move, precisely so the gesture that
         * makes a selection cannot also be the gesture that clears it. */
        sel_clear();
        COMPOSE_SEL = 0;
        if (SETTINGS_ON) {
            /* Modal: it consumes clicks under it, and a click outside closes it. */
            if (inside(R_SET_TZM, MX, MY)) { app_set_tz_index(app_tz_index() - 1); return; }
            if (inside(R_SET_TZP, MX, MY)) { app_set_tz_index(app_tz_index() + 1); return; }
            if (inside(R_SET_THEME, MX, MY)) {
                /* which of the three chips: measured the same way they are drawn */
                static const char *NAME[3] = { "System", "Light", "Dark" };
                int bx = R_SET_THEME.x;
                for (int i = 0; i < 3; i++) {
                    int w = gfx_text_w(NAME[i], F_SM) + 12;
                    if (MX >= bx && MX < bx + w) { app_set_theme(i); return; }
                    bx += w + 6;
                }
                return;
            }
            SETTINGS_ON = 0;
            return;
        }
        if (SYM_ON) {                          /* modal: a tab, a tile, or nothing */
            for (int c = 0; c < CAT_N; c++)
                if (R_CAT[c].w && inside(R_CAT[c], MX, MY)) {
                    SYM_CAT = c;
                    if (SYM_SEL >= pal_n()) SYM_SEL = pal_n() - 1;
                    return;
                }
            for (int i = 0; i < pal_n(); i++)
                if (R_SYM[i].w && inside(R_SYM[i], MX, MY)) {
                    compose_insert_str(pal_at(i)); SYM_ON = 0; return;
                }
            /* A123. ONLY A CLICK OUTSIDE THE SHEET CLOSES. It used to close on ANY click that was
             * not a tile or a tab, and on this hardware that is most of them:
             *
             *   THE CX II HAS NO ARROW KEYS. The touchpad IS the arrow ring, and ns_pointer_feed
             *   turns a low-travel contact into an IN_CLICK -- so a student pressing the pad's
             *   edge to move RIGHT or DOWN lands a click at the cursor's position, which is
             *   usually the gap between tiles, and the palette vanished.
             *
             * Reported from the device as "it keeps exiting when I'm in there and trying to either
             * go right or down with the arrows". A click inside the sheet but on nothing now does
             * nothing, which is what a modal sheet should do anyway. */
            if (!inside(R_SYMSHEET, MX, MY)) { SYM_ON = 0; }
            return;
        }
        if (PICK_ON) {                         /* modal: a click lands on a row or nowhere */
            const ns_store2 *st = app_store();
            for (int i = 0; i < PK_ROWS; i++) {
                if (!R_PROW[i].w || !inside(R_PROW[i], MX, MY)) continue;
                int r = PK.scroll + i, rec = -1;
                PK.sel = r;
                /* ONE PATH for both levels: pk_key decides what the row means, so a suggestion
                 * clicked and a suggestion entered cannot diverge. The earlier version branched on
                 * level here and would have opened a family for a clicked suggestion. */
                if (pk_key(&PK, st, K_ENTER, &rec) == PK_ACT_PICKED && rec >= 0) {
                    /* A129. THE CLICK PATH HAD TO LEARN LIB_MODE TOO. A127 taught the KEY path that
                     * the library inserts rather than sends and left this one sending -- so
                     * clicking a formula asked a question about it instead of putting it in the
                     * box. Two paths to one action and only one of them was updated, which is the
                     * defect this repo keeps paying for; they now agree because both end here. */
                    if (LIB_MODE) {
                        compose_insert_str(st->rec[rec].formula);
                        PICK_ON = 0; LIB_MODE = 0; FIELD_FOCUS = 1;
                    } else picker_send(st->rec[rec].rid);
                }
                return;
            }
            /* A CLICK OUTSIDE DOES NOTHING. The search sheet closes on one, which is right there
             * -- nothing is pending. Here a stray tap would discard a typed question, so leaving
             * is by esc (up a level) or the legend's ask-anyway, both of which say what they do. */
            return;
        }
        if (SEARCH_ON) {                       /* the sheet is modal: it eats clicks under it */
            int rows = NSHIT - SSCROLL; if (rows > SHEET_ROWS) rows = SHEET_ROWS;
            for (int i = 0; i < rows; i++)
                if (inside(R_SROW[i], MX, MY)) { CUR = SHIT[SSCROLL + i]; SCROLL = 0; SEARCH_ON = 0; return; }
            SEARCH_ON = 0;                     /* click outside a row closes, as on the web */
            return;
        }
        if (hit(R_EXIT, MX, MY)) { SETTINGS_ON = !SETTINGS_ON; return; }
        if (BUSY && hit(R_SEND, MX, MY)) { ABORT = 1; return; }   /* Stop, mid-generation */
        /* Answer actions. Tested before the sidebar and the composer because they sit in the
         * transcript, which nothing else claims. inside() rather than hit(): these are three 14px
         * controls 4px apart, and hit()'s 10px slop would let a press between two of them resolve
         * to whichever is nearer -- fine for a lone button in a corner, wrong for a cluster where
         * the neighbour is thumbs-DOWN. */
        if (CUR >= 0) {
            for (int i = 0; i < CHATS[CUR].nturns && i < MAX_TURNS; i++)
                if (R_QACT[i].w > 0 && inside(R_QACT[i], MX, MY)) {
                    toast(clip_set(CHATS[CUR].turn[i].q) ? "Copied" : "Nothing to copy");
                    return;
                }
            for (int i = 0; i < NACT_ROWS && i < CHATS[CUR].nturns; i++) {
                app_turn *t = &CHATS[CUR].turn[i];
                if (inside(R_ACT[i][0], MX, MY)) {
                    toast(clip_set(t->a) ? "Copied" : "Nothing to copy"); return;
                }
                if (inside(R_ACT[i][1], MX, MY)) {
                    t->rating = t->rating > 0 ? 0 : 1;      /* pressing again clears it */
                    if (t->rating) feedback_write(1, t->q, t->a);
                    return;
                }
                if (inside(R_ACT[i][2], MX, MY)) {
                    t->rating = t->rating < 0 ? 0 : -1;
                    if (t->rating) feedback_write(-1, t->q, t->a);
                    return;
                }
            }
        }
        if (hit(R_TOGGLE, MX, MY)) { SIDEBAR = !SIDEBAR; return; }
        if (hit(R_SEARCH, MX, MY)) { open_search(); return; }
        if (SIDEBAR) {
            if (hit(R_NEW, MX, MY)) { start_new_chat(); return; }
            for (int i = 0; i < NCHAT_ROWS; i++) {
                if (inside(R_TRASH[i], MX, MY)) { delete_chat(CHAT_AT[i]); return; }
                if (inside(R_CHAT[i], MX, MY))  { CUR = CHAT_AT[i]; SCROLL = 0; return; }
            }
        }
        /* THE SEND BUTTON IS TESTED FIRST, because R_SEND lies INSIDE R_FIELD. Checking the
         * field first meant every click on the arrow was swallowed as "focus the box" and the
         * message was never sent -- a containment bug, not a hit-testing one, introduced the
         * moment the field became clickable at all. */
        if (hit(R_SEND, MX, MY) && COMPOSE_N && !BUSY) { open_picker(); return; }
        /* Clicking the box makes it the typing target. Typing already went there, but nothing on
         * screen said so, so the bar looked inert until a character appeared in it. */
        if (inside(R_FIELD, MX, MY)) {
            FIELD_FOCUS = 1;
            /* TAP TO PLACE THE CARET.
             *
             * The arrow keys move it too, but on this device they may never arrive: the CX II has
             * no separate arrow keys -- the touchpad IS the arrow ring -- and pointer_poll reads
             * the pad with touchpad_scan(), so an edge press comes through as CONTACT rather than
             * as KEY_NSPIRE_LEFT. Reported from the device as "the cursor moves but the icon
             * doesn't"; the caret index was correct and the key simply never reached the app.
             *
             * A tap does not depend on any of that, and it is the more direct gesture anyway. The
             * offset comes from gfx_text_wrap_hit against the origin and width the text was DRAWN
             * with, so the caret lands under the finger rather than near it. */
            if (COMPOSE_N && TEXT_W > 0) {
                int off = gfx_text_wrap_hit(TEXT_X, TEXT_Y, COMPOSE, F_UI, TEXT_W, COMPOSE_LH,
                                            MX, MY);
                if (off >= 0 && off <= COMPOSE_N) { COMPOSE_C = off; COMPOSE_SEL = 0; }
            }
            return;
        }
        return;
    }

    if (e->kind == IN_KEY) {
        int k = e->key;
        if (SYM_ON) {
            int n = pal_n();
            if (k == K_ESC || k == K_SYM) { SYM_ON = 0; return; }
            if (k == K_TAB)   { SYM_CAT = (SYM_CAT + 1) % CAT_N;
                                if (SYM_SEL >= pal_n()) SYM_SEL = pal_n() - 1; return; }
            /* SYM_SEL == -1 is the tab row. Left/right there change category; anywhere else they
             * move within the grid. One chain, so there is no mode to get stuck in. */
            if (SYM_SEL < 0) {
                if (k == K_LEFT)  { SYM_CAT = (SYM_CAT + CAT_N - 1) % CAT_N; return; }
                if (k == K_RIGHT) { SYM_CAT = (SYM_CAT + 1) % CAT_N; return; }
                if (k == K_DOWN || k == K_ENTER) { SYM_SEL = 0; return; }
                return;
            }
            if (k == K_LEFT)  { if (SYM_SEL > 0) SYM_SEL--; return; }
            if (k == K_RIGHT) { if (SYM_SEL + 1 < n) SYM_SEL++; return; }
            if (k == K_UP)    { SYM_SEL = (SYM_SEL - SYM_COLS >= 0) ? SYM_SEL - SYM_COLS : -1; return; }
            if (k == K_DOWN)  { if (SYM_SEL + SYM_COLS < n) SYM_SEL += SYM_COLS; return; }
            if (k == K_ENTER) { compose_insert_str(pal_at(SYM_SEL)); SYM_ON = 0; return; }
            return;
        }
        if (k == K_SYM) { SYM_ON = !SYM_ON; SYM_SEL = 0; return; }
        if (k == K_LIB) { open_library(); return; }     /* A127: the catalog key = the book icon */

        /* THE PICKER IS MODAL AND CONSUMES EVERY KEY. It is checked before the search sheet and
         * before ESC's own chain: an ESC that reached that chain would clear the composer or go
         * home, when in the picker it means "up one level" or "ask anyway". */
        if (PICK_ON) {
            const ns_store2 *st = app_store();
            int rec = -1;
            pk_action a = pk_key(&PK, st, k, &rec);
            if (a == PK_ACT_PICKED && rec >= 0) {
                if (LIB_MODE) {            /* A127: the library INSERTS; it never asks a question */
                    compose_insert_str(st->rec[rec].formula);
                    PICK_ON = 0; LIB_MODE = 0; FIELD_FOCUS = 1;
                } else picker_send(st->rec[rec].rid);
            }
            else if (a == PK_ACT_ASK_ANYWAY) {
                if (LIB_MODE) { PICK_ON = 0; LIB_MODE = 0; }   /* esc just closes the library */
                else picker_send(0);
            }
            return;
        }
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
            /* ESC NO LONGER QUITS. It interrupts, then clears the box, then goes home, and stops
             * there. One stray press on the home screen used to end the app outright, and that is
             * exactly what happened when the main enter key turned out to be unmapped: enter did
             * nothing, ESC was the next thing tried, and the second press exited. Leaving is the X
             * button, which is on screen and now has a hitbox, or ctrl+Q. */
            if (SETTINGS_ON)      { SETTINGS_ON = 0; return; }
            if (BUSY)             { ABORT = 1; return; }   /* interrupt, never navigate away */
            /* ESC out of the BAR keeps what you typed. Discarding a half-written question because
             * someone stepped back from the box is destroying work to undo a focus change. A
             * second press, once the box is no longer the target, clears it. */
            else if (FIELD_FOCUS)  { FIELD_FOCUS = 0; return; }
            else if (COMPOSE_N)   { compose_clear(); return; }
            else if (CUR >= 0)    { CUR = -1; return; }
            return;
        }
        /* ctrl+ESC, not ctrl+Q. ESC is already the "get out of the thing I am in" key, so ESC
         * with a modifier is the natural "get out of the app", and it cannot be reached by
         * accident the way a bare ESC could. */
        if (k == K_QUIT) { QUIT = 1; return; }
        if (k == K_TAB)  { SIDEBAR = !SIDEBAR; return; }
        /* Chords work from anywhere, including mid-compose, which is the point of having them. */
        if (k == K_NEW)    { start_new_chat(); return; }
        if (k == K_SEARCH) { open_search(); return; }
        if (k == K_PANEL)  { SIDEBAR = !SIDEBAR; return; }
        if (k == K_PASTE)  { clip_paste(); return; }
        if (k == K_SELALL) {
            /* THE DRAFT FIRST. If there is text in the box, that is what you are working on and
             * that is what select-all means -- pick it up, or wipe it and start again. Keyed on
             * the text rather than on FIELD_FOCUS because typing on the keypad fills the box
             * without ever clicking it, and that is exactly the case this is for.
             *
             * With an empty box it falls through to the transcript, which is the only other thing
             * on screen that could be meant. */
            if (COMPOSE_N) { COMPOSE_SEL = 1; return; }
            if (CUR < 0 || CHATS[CUR].nturns == 0) { toast("Nothing to select"); return; }
            SEL_TURN = SEL_SPAN = -1; SEL_A = SEL_B = 0;
            SEL_ALL = 1;
            return;
        }
        if (k == K_COPY)   {
            /* Selection first, since that is what the reader just made. Failing that, the answer
             * under the pointer, so ctrl+c does the obvious thing without a drag. */
            if (COMPOSE_SEL && COMPOSE_N) {
                toast(clip_set(COMPOSE) ? "Copied" : "Nothing to copy"); return;
            }
            if (sel_active()) {
                const char *s = sel_text();
                if (!s || !clip_set(s)) { toast("Nothing to copy"); return; }
                /* A conversation can outgrow the clipboard. Saying "Copied" for a copy that lost
                 * its tail is the same silence as a paste that trims without a word. */
                toast((int)strlen(s) > (int)sizeof CLIP - 1 ? "Copied, trimmed to fit" : "Copied");
                return;
            }
            const char *a = answer_under_pointer();
            toast(a && clip_set(a) ? "Copied" : "Select some text first");
            return;
        }
        if (k == K_BACK) {
            /* Selected text deletes WHOLE. Removing one character from a full selection is what a
             * field that merely drew a highlight would do, and it is never what was meant. */
            if (COMPOSE_SEL) { compose_clear(); return; }
            if (COMPOSE_C > 0 && COMPOSE_N > 0) {
                memmove(COMPOSE + COMPOSE_C - 1, COMPOSE + COMPOSE_C, (size_t)(COMPOSE_N - COMPOSE_C));
                COMPOSE_C--; COMPOSE_N--; COMPOSE[COMPOSE_N] = 0;
            }
            return;
        }
        if (k == K_ENTER) {
            if (COMPOSE_N && !BUSY) { open_picker(); return; }
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
        if (k == K_LEFT)  { if (COMPOSE_C > 0) COMPOSE_C--; COMPOSE_SEL = 0; return; }
        if (k == K_RIGHT) { if (COMPOSE_C < COMPOSE_N) COMPOSE_C++; COMPOSE_SEL = 0; return; }
        if (k >= 32 && k < 127 && COMPOSE_N < (int)sizeof COMPOSE - 1) {
            /* A keystroke REPLACES the selection rather than appending to it. */
            if (COMPOSE_SEL) compose_clear();
            if (COMPOSE_C < 0) COMPOSE_C = 0;
            if (COMPOSE_C > COMPOSE_N) COMPOSE_C = COMPOSE_N;
            memmove(COMPOSE + COMPOSE_C + 1, COMPOSE + COMPOSE_C, (size_t)(COMPOSE_N - COMPOSE_C));
            COMPOSE[COMPOSE_C++] = (char)k;
            COMPOSE_N++; COMPOSE[COMPOSE_N] = 0;
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
    if (SYM_ON) return 1;                       /* modal */
    if (PICK_ON) return 1;                      /* modal, exactly like the sheet below */
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
