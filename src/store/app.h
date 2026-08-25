#ifndef NS_APP_H
#define NS_APP_H
/* The ChatTLM application: layout, state, input handling. Draws through gfx.c, so it renders
 * identically on the device and in the host harness. */
#include "gfx.h"

/* palette, kept in step with tools/webui/index.html */
#define C_BG      HEX(0xFFFFFF)
#define C_SIDE    HEX(0xF9F9F9)
#define C_LINE    HEX(0xE5E5E5)
#define C_INK     HEX(0x0D0D0D)
#define C_INK2    HEX(0x5D5D5D)
#define C_INK3    HEX(0x8F8F8F)
#define C_BUBBLE  HEX(0xF4F4F4)
#define C_SEL     HEX(0xECECEC)
#define C_TOOL    HEX(0xF5F6F8)
#define C_TOOLLN  HEX(0xE3E5EA)
#define C_RES     HEX(0xEDF7F0)
#define C_RESLN   HEX(0xCFE8D8)
#define C_RESFG   HEX(0x186A3B)
#define C_ERR     HEX(0xFDF2F2)
#define C_ERRFG   HEX(0xA8342C)

/* 320x240 is a fifth the width of the desktop layout, so proportions are re-derived rather than
 * scaled: an 18%-wide sidebar would be 58px and unreadable, a scaled-down 260px would eat a
 * third of the screen. 88px holds ~13 characters of a chat title, which is the useful minimum. */
#define SIDE_W   88
#define TOP_H    24
#define DOCK_H   40
#define PAD      8

typedef enum { IN_NONE = 0, IN_MOVE, IN_CLICK, IN_KEY, IN_SCROLL } in_kind;
typedef struct {
    in_kind kind;
    int x, y;          /* cursor position, screen pixels */
    int hover;         /* pointer is near the pad but not pressed -- drives hover states */
    int key;           /* ASCII, or one of the K_ codes below */
    int dy;            /* scroll delta */
} in_event;

#define K_UP   0x101
#define K_DOWN 0x102
#define K_ESC  0x1B
#define K_ENTER 0x0D
#define K_TAB  0x09
#define K_BACK 0x08

typedef struct {
    char  q[160];      /* the question as typed */
    char  a[512];      /* the model's raw output, span markup included */
    /* Compact form, captured AT GENERATION TIME while the record and values are still in hand:
     * "v=d/t d=150 t=12 -> 12.5". Reconstructing this later by re-parsing `a` would depend on the
     * model having produced well-formed markup, which is exactly what fails 50% of the time. */
    char  sum[72];
    int   done;
} app_turn;

#define MAX_TURNS 16
#define MAX_CHATS 12

typedef struct {
    char title[64];
    app_turn turn[MAX_TURNS];
    int nturns;
    int used;
} app_chat;

void app_init(void);
void app_event(const in_event *e);
void app_draw(void);
int  app_should_quit(void);
/* the app asks the host/device for generation; implemented separately on each side */
void app_request(const char *question, const char *rid);
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
/* Build the context string for the next turn, newest-first until `budget_chars` is spent.
 * Recent turns go in verbatim; older ones fall back to their compact form; anything that still
 * does not fit is dropped silently. Returns characters written. */
int  app_context(char *out, int cap, int budget_chars);
/* Build the compact summary from the finished turn. Call BEFORE app_stream_end(). */
void app_finish_turn(const char *formula, const char *values);
#endif
