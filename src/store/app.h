#ifndef NS_APP_H
#define NS_APP_H
/* The ChatTLM application: layout, state, input handling. Draws through gfx.c, so it renders
 * identically on the device and in the host harness. */
#include "gfx.h"
#include "loader.h"

/* palette, kept in step with tools/webui/index.html */
/* ---- palette ------------------------------------------------------------------------------------
 * These were #defines, i.e. compile-time constants, which is fine for one theme and impossible for
 * two. They are now indices into a runtime table, so every existing call site is unchanged and the
 * whole UI re-colours by swapping one array.
 *
 * The six colours that were written inline in app.c -- the modal scrim, the trash hover, the
 * scrollbar, the exit hover, the composer outline, the disabled send -- are named here too. On the
 * web the same class of hardcoded colour was every place dark mode broke; naming them is the fix
 * before the bug rather than after it. */
enum {
    P_BG, P_SIDE, P_LINE, P_INK, P_INK2, P_INK3, P_BUBBLE, P_SEL,
    P_TOOL, P_TOOLLN, P_RES, P_RESLN, P_RESFG, P_ERR, P_ERRFG,
    P_SCRIM, P_TRASH_HOT, P_BAR, P_EXIT_HOT, P_FIELD, P_FIELD_LN, P_SEND_OFF, P_SHEET,
    P_DANGER,          /* destructive intent: the trash under a direct pointer */
    P_SELTEXT,         /* the band behind selected text */
    P_N
};
extern uint16_t TLM_PAL[P_N];

#define C_BG        TLM_PAL[P_BG]
#define C_SIDE      TLM_PAL[P_SIDE]
#define C_LINE      TLM_PAL[P_LINE]
#define C_INK       TLM_PAL[P_INK]
#define C_INK2      TLM_PAL[P_INK2]
#define C_INK3      TLM_PAL[P_INK3]
#define C_BUBBLE    TLM_PAL[P_BUBBLE]
#define C_SEL       TLM_PAL[P_SEL]
#define C_TOOL      TLM_PAL[P_TOOL]
#define C_TOOLLN    TLM_PAL[P_TOOLLN]
#define C_RES       TLM_PAL[P_RES]
#define C_RESLN     TLM_PAL[P_RESLN]
#define C_RESFG     TLM_PAL[P_RESFG]
#define C_ERR       TLM_PAL[P_ERR]
#define C_ERRFG     TLM_PAL[P_ERRFG]
#define C_SCRIM     TLM_PAL[P_SCRIM]
#define C_TRASH_HOT TLM_PAL[P_TRASH_HOT]
#define C_BAR       TLM_PAL[P_BAR]
#define C_EXIT_HOT  TLM_PAL[P_EXIT_HOT]
/* The composer's fill, and NOT reusable as C_BUBBLE. They coincide in dark (both #303030) and
 * differ in light (#ffffff vs #f4f4f4), so sharing one token would have been correct on exactly
 * the theme anyone checked first. */
#define C_FIELD     TLM_PAL[P_FIELD]
#define C_FIELD_LN  TLM_PAL[P_FIELD_LN]
#define C_SEND_OFF  TLM_PAL[P_SEND_OFF]
/* Modal surface. NOT the same token as the page, which is the mistake it exists to prevent: a
 * scrim cannot darken a near-black ground -- 28% of nothing is nothing once RGB565 quantises it --
 * so in dark the sheet must LIFT off the page instead of the page sinking behind it. */
#define C_SHEET     TLM_PAL[P_SHEET]
#define C_DANGER    TLM_PAL[P_DANGER]
#define C_SELTEXT   TLM_PAL[P_SELTEXT]

/* Three states, cycled by the header button, matching the web exactly:
 *   TH_AUTO   follow the device -- on the calculator that means the CLOCK, since the Nspire OS has
 *             no appearance setting to follow
 *   TH_LIGHT  always day
 *   TH_DARK   always night */
enum { TH_AUTO = 0, TH_LIGHT, TH_DARK };
void app_set_theme(int mode);
/* Feed the app a millisecond clock. Animation is timed from THIS, not from how often app_draw
 * happens to run -- draws are driven by input, so a frame count raced under a finger and froze
 * without one. Returns 1 when something on screen has moved since the last call and a redraw is
 * therefore owed; the caller uses that to decide whether to spend a frame. */
int  app_set_now(unsigned ms);
int  app_theme(void);
/* What AUTO currently resolves to. `hour` is 0-23, or negative when the clock could not be read --
 * in which case this returns light, because guessing dark on an unknown clock is worse than a
 * wrong-but-legible default. */
int  app_auto_is_dark(int hour);
/* Hours from UTC. The device has no timezone and its RTC is a bare seconds counter, so AUTO cannot
 * tell local day from night without being told this. */
int  app_tz(void);              /* hours from UTC for the selected zone */
const char *app_tz_name(void);  /* "HST", "PST", ... as the reader would say it */
int  app_tz_index(void);
void app_set_tz_index(int i);

/* Local hour 0-23, or NEGATIVE when there is no usable clock.
 *
 * Supplied by the platform, because there is no portable answer: the calculator reads the RTC at
 * 0x90090000 and the host harness has no clock at all. Returning -1 rather than a plausible-looking
 * default is the point -- app_auto_is_dark() then chooses light, instead of a theme derived from a
 * number nobody measured. bench/bench_rtc.c is the probe that will establish whether that register
 * is running on this unit and on what epoch; until it has run, the device implementation reports
 * -1 and AUTO is light. */
int  app_clock_hour(void);

/* 320x240 is a fifth the width of the desktop layout, so proportions are re-derived rather than
 * scaled: an 18%-wide sidebar would be 58px and unreadable, a scaled-down 260px would eat a
 * third of the screen. 88px holds ~13 characters of a chat title, which is the useful minimum. */
/* 80, not 88. With the footer gone and the theme control gone the column carries three 20px icons
 * and a list; 88 left dead space at both ends of the icon band and took 8px the transcript could
 * use. At 80 the three icons distribute with equal 5px gaps -- 3*20 + 4*5 = 80 exactly. */
/* 88 again, and deliberately. 24px icon plates need 3*24 + 4*4 = 88 to sit on even 4px gaps; at 80
 * the gaps collapse to 2 and the band looks crammed. The 8px goes back to the sidebar because
 * legible controls matter more here than eight columns of transcript. */
#define SIDE_W   88
/* 26, not 24. The band carries 20px icons, and at 24 they had 2px of air above and below -- they
 * read as jammed into the corner rather than placed. The F_BIG title still clears it: drawn at
 * y=4 with an 18px box, so 4..22 inside 26. */
/* 30, for the 24px plates: y=3 puts them at 3..27 with 3px under, and the F_UIB title box of 15
 * centres at (30-15)/2 = 7. */
#define TOP_H    30
/* 43, not 40. The dock carries a 24px field AND the disclaimer line beneath it: 3 + 24 + 2 + 13
 * fills 41 of 43. At 40 the field had to stay 20px tall, which is where the composer's proportions
 * went wrong -- the web field is 54px against a 34px send button, a ratio of 1.59, and 20px against
 * a 14px button is 1.43. At 24 it is 1.71 and the pill reads as a pill again. */
#define DOCK_H   43
#define PAD      8

typedef enum { IN_NONE = 0, IN_MOVE, IN_CLICK, IN_KEY, IN_SCROLL } in_kind;
typedef struct {
    in_kind kind;
    int x, y;          /* cursor position, screen pixels */
    int hover;         /* pointer is near the pad but not pressed -- drives hover states */
    /* The touchpad's PHYSICAL click, which is a different thing from `hover`: contact is a finger
     * resting on the pad, pressed is that finger pushing it down. Hover moves the cursor; pressed
     * and moving drags a selection. Without the distinction there is no gesture left for
     * selecting, because plain movement is already spoken for. */
    int pressed;
    int key;           /* ASCII, or one of the K_ codes below */
    int dy;            /* scroll delta */
} in_event;

#define K_UP   0x101
#define K_DOWN 0x102
#define K_ESC  0x1B
#define K_ENTER 0x0D
#define K_TAB  0x09
#define K_LEFT 0x103
#define K_RIGHT 0x104
#define K_BACK 0x08
/* Chords. Ctrl is a modifier on the Nspire keypad, so the poll reports these as their own codes
 * rather than as a flag -- the app never has to know how the hardware spells "held". */
#define K_NEW    0x110      /* ctrl+N */
#define K_SEARCH 0x111      /* ctrl+S */
#define K_PANEL  0x112      /* ctrl+B: show or hide the side panel */
#define K_QUIT   0x113      /* ctrl+Q: leave, since ESC no longer does */
#define K_COPY   0x114
#define K_PASTE  0x115
#define K_SELALL 0x116

typedef struct {
    char  q[160];      /* the question as typed */
    char  a[512];      /* the model's raw output, span markup included */
    /* Compact form, captured AT GENERATION TIME while the record and values are still in hand:
     * "v=d/t d=150 t=12 -> 12.5". Reconstructing this later by re-parsing `a` would depend on the
     * model having produced well-formed markup, which is exactly what fails 50% of the time. */
    char  sum[72];
    int   done;
    /* -1 down, 0 unrated, +1 up. IN MEMORY ONLY, deliberately: persisting it would change
     * chat_load's field order and silently discard every chat already on the device, and the
     * durable half of this signal is the feedback file, which app_set_feedback appends to on every
     * press. The highlight is a receipt for the click, not the record. */
    signed char rating;
} app_turn;

#define MAX_TURNS 16
#define MAX_CHATS 12

typedef struct {
    char title[64];
    app_turn turn[MAX_TURNS];
    int nturns;
    int used;
} app_chat;

/* Where thumbs-up/down land. Append-only, one line per press, so a rating survives even though
 * the highlight does not. Pass 0 to disable (the host tests have no filesystem to write to). */
void app_set_feedback(const char *path);
void app_init(void);
void app_event(const in_event *e);
void app_draw(void);
int  app_should_quit(void);
/* the app asks the host/device for generation; implemented separately on each side */
/* `rid` is the record the STUDENT picked, or 0 for Form C -- "no matching relation". It is not
 * optional and it is not a hint: at 9.5% measured retrieval the runtime does not get to guess. */
void app_request(const char *question, const char *rid);

/* The loaded relation store, for the picker. Owned by whoever loaded it (device_app.c on the
 * calculator, the host stub otherwise) and never freed by the UI. Returns 0 if none is loaded --
 * the picker draws that state rather than pretending to an empty store. */
const ns_store2 *app_store(void);
/* Where sessions are kept between runs. Set once at startup; app.c writes after every change that
 * can lose data. Passing NULL (the host harness) disables persistence entirely. */
void app_set_persist(const char *path);
void app_stream_token(const char *piece);   /* called as tokens arrive */
void app_stream_end(void);
/* Interruption. A generation loop is the only thing here that runs long enough that the UI stops
 * answering -- 60 tokens at the measured 2.683 tok/s is 22 seconds -- so it must poll. */
int  app_take_abort(void);      /* 1 if ESC or Stop was pressed; clears on read */
int  app_busy(void);

/* ---- live status --------------------------------------------------------------------------------
 * What the runtime is doing right now, shown in grey above the answer. This matters MORE here than
 * on the web: a turn is 134 ms there and roughly 22 s on the calculator, so the difference between
 * "working" and "frozen" is the whole of the user's experience.
 *
 * Every label must correspond to real work. A line claiming a tool ran when none did is the same
 * defect as a result chip holding a number the model invented -- so app_status is called from the
 * generation loop at points where something actually happened, never on a timer.
 *
 * `mono` is the detail drawn in the lighter face: a record name, a call, a result. NULL for none. */
void app_status(const char *label, const char *mono);
/* Record what the finished turn actually did, for the line that stays. ms is MEASURED elapsed time;
 * pass tool_call = NULL when no tool ran, and it will not claim one. */
void app_status_done(unsigned ms, const char *tool_call, const char *tool_result, int tool_ok);
int  app_hit_stop(int x, int y);
/* Is (x,y) on something clickable? The device uses it so ENTER can activate whatever the cursor is
 * resting on, which is how you commit a click without a tap moving the cursor first. */
int  app_hit_control(int x, int y);
/* Build the context string for the next turn, newest-first until `budget_chars` is spent.
 * Recent turns go in verbatim; older ones fall back to their compact form; anything that still
 * does not fit is dropped silently. Returns characters written. */
int  app_context(char *out, int cap, int budget_chars);
/* Build the compact summary from the finished turn. Call BEFORE app_stream_end(). */
void app_finish_turn(const char *formula, const char *values);
#endif
