#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "app.h"

/* ---- state ---------------------------------------------------------------------------------- */
static app_chat CHATS[MAX_CHATS];
static int NCHATS, CUR = -1;
static int SCROLL;                 /* transcript scroll offset in pixels */
static int MX = 160, MY = 120;     /* cursor */
static int HOVER;                  /* pointer in proximity: hover states are live */
static int QUIT;
static int SIDEBAR = 1;
static char COMPOSE[160];          /* what is being typed */
static int  COMPOSE_N;
static int  BUSY;

/* hit regions, recomputed every frame so hover testing and click handling can never disagree
 * about where something is -- they read the same rectangles. */
static gfx_rect R_TOGGLE, R_NEW, R_CHAT[MAX_CHATS], R_TRASH[MAX_CHATS], R_FIELD, R_SEND;
static int NCHAT_ROWS;

static void clamp_scroll(int content_h, int view_h);

static int inside(gfx_rect r, int x, int y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void app_init(void) {
    NCHATS = 0; CUR = -1; SCROLL = 0; COMPOSE[0] = 0; COMPOSE_N = 0;
}
int app_should_quit(void) { return QUIT; }

static app_chat *new_chat(const char *title) {
    if (NCHATS >= MAX_CHATS) {          /* oldest out; sessions are bounded on a 21 MB heap */
        memmove(&CHATS[0], &CHATS[1], sizeof(app_chat) * (MAX_CHATS - 1));
        NCHATS = MAX_CHATS - 1;
    }
    app_chat *c = &CHATS[NCHATS];
    memset(c, 0, sizeof *c);
    snprintf(c->title, sizeof c->title, "%s", title);
    c->used = 1;
    CUR = NCHATS;
    NCHATS++;
    return c;
}
static void delete_chat(int i) {
    if (i < 0 || i >= NCHATS) return;
    memset(&CHATS[i], 0, sizeof(app_chat));      /* wiped, not just unlinked */
    memmove(&CHATS[i], &CHATS[i + 1], sizeof(app_chat) * (NCHATS - i - 1));
    NCHATS--;
    if (CUR == i) CUR = -1; else if (CUR > i) CUR--;
}

/* ---- span markup ------------------------------------------------------------------------------
 * The model emits <tool>eval<arg>EXPR</tool><res>V</res><a>PROSE<end>. Rendering walks the string
 * once and draws each span in its own style -- there is no intermediate parse tree, because the
 * text is being appended token by token while it streams and a tree would be rebuilt every frame. */
typedef enum { SP_TOOL, SP_RES, SP_TEXT } sp_kind;

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
    while (*e && *e != '<') e++;
    int n = (int)(e - s); if (n >= cap) n = cap - 1;
    memcpy(out, s, (size_t)n); out[n] = 0;
    *k = SP_TEXT;
    if (!*e) return e;
    if (!strncmp(e, "<a>", 3)) return e + 3;
    if (!strncmp(e, "<end>", 5)) return e + 5;
    return e + 1;
}

/* "eval<arg> (150.0)/(12.0)" -> fn "eval", arg "(150.0)/(12.0)" */
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
}

/* ---- drawing --------------------------------------------------------------------------------- */
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
static int draw_answer(int x, int y, int w, const char *raw, int draw) {
    int lh = gfx_font_h(F_UI) + 2, cy = y;
    const char *p = raw;
    char buf[512]; sp_kind k;
    int chip_x = x, row_open = 0;
    while ((p = span_next(p, &k, buf, sizeof buf)) != 0) {
        if (k == SP_TOOL) {
            char fn[24], arg[256];
            split_call(buf, fn, sizeof fn, arg, sizeof arg);
            int cw = chip(chip_x, cy, fn, arg, C_TOOL, C_TOOLLN, C_INK, draw);
            chip_x += cw + 4; row_open = 1;
        } else if (k == SP_RES) {
            const char *v = buf; while (*v == ' ') v++;
            int err = (*v == '!');
            char lbl[64];
            snprintf(lbl, sizeof lbl, "%s%.58s", err ? "" : "= ", v);
            int cw = chip(chip_x, cy, 0, lbl, err ? C_ERR : C_RES, err ? C_ERR : C_RESLN,
                          err ? C_ERRFG : C_RESFG, draw);
            chip_x += cw + 4; row_open = 1;
        } else {
            const char *t = buf; while (*t == ' ') t++;
            if (!*t) continue;
            if (row_open) { cy += gfx_font_h(F_UI) + 8; chip_x = x; row_open = 0; }
            int n = gfx_text_wrap(x, cy, t, F_UI, C_INK, C_BG, w, lh, draw);
            cy += n * lh;
        }
        if (!*p) break;
    }
    if (row_open) cy += gfx_font_h(F_UI) + 8;
    return cy - y;
}

static void draw_sidebar(void) {
    gfx_fill(0, 0, SIDE_W, GFX_H, C_SIDE);
    gfx_vline(SIDE_W, 0, GFX_H, C_LINE);

    /* header: toggle on the right, matching the main bar's height so both sit on one line */
    R_TOGGLE = (gfx_rect){ SIDE_W - 22, 4, 18, 16 };
    uint16_t tg = (HOVER && inside(R_TOGGLE, MX, MY)) ? C_SEL : C_SIDE;
    gfx_rrect(R_TOGGLE.x - 2, R_TOGGLE.y - 2, R_TOGGLE.w + 4, R_TOGGLE.h + 4, 4, tg);
    gfx_rrect_outline(R_TOGGLE.x + 2, R_TOGGLE.y + 2, 14, 12, 2, C_INK2);
    gfx_vline(R_TOGGLE.x + 7, R_TOGGLE.y + 2, 12, C_INK2);

    R_NEW = (gfx_rect){ 4, TOP_H + 2, SIDE_W - 8, 18 };
    if (HOVER && inside(R_NEW, MX, MY)) gfx_rrect(R_NEW.x, R_NEW.y, R_NEW.w, R_NEW.h, 5, C_SEL);
    gfx_text(R_NEW.x + 6, R_NEW.y + 2, "New chat", F_UIB, C_INK,
             (HOVER && inside(R_NEW, MX, MY)) ? C_SEL : C_SIDE);

    gfx_text(10, TOP_H + 26, "Chats", F_UI, C_INK3, C_SIDE);

    NCHAT_ROWS = NCHATS;
    for (int i = 0; i < NCHATS; i++) {
        int y = TOP_H + 42 + i * 18;
        if (y > GFX_H - DOCK_H - 20) { NCHAT_ROWS = i; break; }
        R_CHAT[i]  = (gfx_rect){ 4, y - 2, SIDE_W - 8, 17 };
        R_TRASH[i] = (gfx_rect){ SIDE_W - 20, y - 1, 14, 14 };
        int hot = HOVER && inside(R_CHAT[i], MX, MY);
        uint16_t bg = (i == CUR || hot) ? C_SEL : C_SIDE;
        if (i == CUR || hot) gfx_rrect(R_CHAT[i].x, R_CHAT[i].y, R_CHAT[i].w, R_CHAT[i].h, 5, bg);
        gfx_text_ellipsis(10, y, CHATS[i].title, F_UI, C_INK, bg, SIDE_W - (hot ? 32 : 18));
        if (hot) {                        /* trash appears only on hover, as on the web */
            int tx = R_TRASH[i].x, ty = R_TRASH[i].y;
            uint16_t tb = inside(R_TRASH[i], MX, MY) ? HEX(0xE6E6E6) : bg;
            gfx_rrect(tx, ty, 14, 14, 3, tb);
            gfx_hline(tx + 3, ty + 4, 9, C_INK2);      /* lid */
            gfx_hline(tx + 6, ty + 2, 3, C_INK2);      /* handle */
            gfx_vline(tx + 4, ty + 5, 7, C_INK2);      /* body sides */
            gfx_vline(tx + 10, ty + 5, 7, C_INK2);
            gfx_hline(tx + 4, ty + 11, 7, C_INK2);     /* base */
            gfx_vline(tx + 7, ty + 6, 5, C_INK2);      /* tine */
        }
    }

    gfx_hline(0, GFX_H - DOCK_H, SIDE_W, C_LINE);
    gfx_text(8, GFX_H - DOCK_H + 5,  "396 MHz",    F_UI, C_INK2, C_SIDE);
    gfx_text(8, GFX_H - DOCK_H + 18, "2.68 tok/s", F_UI, C_INK3, C_SIDE);
}

static void draw_main(void) {
    int x0 = SIDEBAR ? SIDE_W + 1 : 0;
    int w  = GFX_W - x0;
    gfx_fill(x0, 0, w, GFX_H, C_BG);

    /* top bar: ChatSLM when empty, the chat's title inside a chat */
    const char *title = (CUR >= 0) ? CHATS[CUR].title : "ChatSLM";
    gfx_text_ellipsis(x0 + PAD, 4, title, F_BIG, C_INK, C_BG, w - 2 * PAD - 34);
    gfx_hline(x0, TOP_H, w, C_LINE);

    /* transcript */
    int px0 = x0 + PAD, pw = w - 2 * PAD;
    int top = TOP_H + 1, bot = GFX_H - DOCK_H;
    gfx_clip(x0, top, w, bot - top);

    if (CUR < 0) {
        gfx_text(px0, top + 26, "What can I work out?", F_BIG, C_INK, C_BG);
        gfx_text(px0, top + 50, "Pick a relation, give values,", F_UI, C_INK3, C_BG);
        gfx_text(px0, top + 64, "get a worked answer.", F_UI, C_INK3, C_BG);
    } else {
        app_chat *c = &CHATS[CUR];
        int lh0 = gfx_font_h(F_UI) + 2, total = 6;
        for (int i = 0; i < c->nturns; i++) {
            app_turn *t = &c->turn[i];
            total += gfx_text_wrap(0, 0, t->q, F_UI, C_INK, C_BUBBLE, pw - 26, lh0, 0) * lh0 + 8 + 8;
            total += t->a[0] ? draw_answer(0, 0, pw, t->a, 0) + 10 : 20;
        }
        clamp_scroll(total, bot - top);
        int y = top + 6 - SCROLL;
        for (int i = 0; i < c->nturns; i++) {
            app_turn *t = &c->turn[i];
            /* question, right-aligned in a bubble */
            int lh = gfx_font_h(F_UI) + 2;
            int lines = gfx_text_wrap(0, 0, t->q, F_UI, C_INK, C_BUBBLE, pw - 26, lh, 0);
            int bh = lines * lh + 8;
            gfx_rrect(px0 + 16, y, pw - 16, bh, 7, C_BUBBLE);
            gfx_text_wrap(px0 + 24, y + 4, t->q, F_UI, C_INK, C_BUBBLE, pw - 32, lh, 1);
            y += bh + 8;
            if (t->a[0]) y += draw_answer(px0, y, pw, t->a, 1) + 10;
            else { gfx_fill(px0, y + 4, 5, 9, C_INK); y += 20; }   /* streaming caret */
        }
    }
    gfx_clip_reset();

    /* composer */
    int cy = GFX_H - DOCK_H + 3;
    R_FIELD = (gfx_rect){ x0 + PAD, cy, w - 2 * PAD, 20 };
    gfx_rrect(R_FIELD.x, R_FIELD.y, R_FIELD.w, R_FIELD.h, 10, C_BG);
    gfx_rrect_outline(R_FIELD.x, R_FIELD.y, R_FIELD.w, R_FIELD.h, 10, HEX(0xD9D9D9));
    if (COMPOSE_N) {
        gfx_text_ellipsis(R_FIELD.x + 9, cy + 3, COMPOSE, F_UI, C_INK, C_BG, R_FIELD.w - 34);
        int cw = gfx_text_w(COMPOSE, F_UI);
        if (cw < R_FIELD.w - 40) gfx_vline(R_FIELD.x + 9 + cw + 1, cy + 4, 12, C_INK);
    } else {
        gfx_text(R_FIELD.x + 9, cy + 3, "Ask a physics question", F_UI, C_INK3, C_BG);
    }
    R_SEND = (gfx_rect){ R_FIELD.x + R_FIELD.w - 20, cy + 2, 16, 16 };
    uint16_t sb = COMPOSE_N ? C_INK : HEX(0xD5D5D5);
    gfx_rrect(R_SEND.x, R_SEND.y, R_SEND.w, R_SEND.h, 8, sb);
    for (int i = 0; i < 5; i++) gfx_hline(R_SEND.x + 8 - i, R_SEND.y + 5 + i, 1, C_BG);
    gfx_vline(R_SEND.x + 8, R_SEND.y + 5, 7, C_BG);

    gfx_text(x0 + (w - gfx_text_w("ChatSLM can make mistakes.", F_UI)) / 2, GFX_H - 14,
             "ChatSLM can make mistakes.", F_UI, C_INK3, C_BG);
}

static void draw_cursor(void) {
    /* a plain arrow, drawn last so it is never occluded. Outlined in white so it stays visible
     * over both the white pane and the grey sidebar. */
    for (int i = 0; i < 10; i++) {
        int wdt = 1 + i * 6 / 10;
        gfx_hline(MX, MY + i, wdt + 1, C_BG);
        gfx_hline(MX, MY + i, wdt, C_INK);
    }
    gfx_hline(MX, MY + 10, 4, C_BG);
}

void app_draw(void) {
    gfx_clear(C_BG);
    if (SIDEBAR) draw_sidebar();
    draw_main();
    draw_cursor();
    gfx_present();
}

/* ---- input ------------------------------------------------------------------------------------ */
void app_event(const in_event *e) {
    if (e->kind == IN_MOVE)  { MX = e->x; MY = e->y; HOVER = e->hover; return; }
    if (e->kind == IN_SCROLL){ SCROLL += e->dy; if (SCROLL < 0) SCROLL = 0; return; }

    if (e->kind == IN_CLICK) {
        MX = e->x; MY = e->y;
        if (inside(R_TOGGLE, MX, MY)) { SIDEBAR = !SIDEBAR; return; }
        if (SIDEBAR) {
            if (inside(R_NEW, MX, MY)) { CUR = -1; SCROLL = 0; COMPOSE_N = 0; COMPOSE[0] = 0; return; }
            for (int i = 0; i < NCHAT_ROWS; i++) {
                if (inside(R_TRASH[i], MX, MY)) { delete_chat(i); return; }
                if (inside(R_CHAT[i], MX, MY))  { CUR = i; SCROLL = 0; return; }
            }
        }
        if (inside(R_SEND, MX, MY) && COMPOSE_N && !BUSY) {
            app_request(COMPOSE, 0); COMPOSE_N = 0; COMPOSE[0] = 0; return;
        }
        return;
    }

    if (e->kind == IN_KEY) {
        int k = e->key;
        if (k == K_ESC)  { if (CUR >= 0) CUR = -1; else QUIT = 1; return; }
        if (k == K_TAB)  { SIDEBAR = !SIDEBAR; return; }
        if (k == K_BACK) { if (COMPOSE_N) COMPOSE[--COMPOSE_N] = 0; return; }
        if (k == K_ENTER){ if (COMPOSE_N && !BUSY) { app_request(COMPOSE, 0); COMPOSE_N = 0; COMPOSE[0] = 0; } return; }
        if (k == K_DOWN) { SCROLL += 16; return; }
        if (k == K_UP)   { SCROLL -= 16; if (SCROLL < 0) SCROLL = 0; return; }
        if (k >= 32 && k < 127 && COMPOSE_N < (int)sizeof COMPOSE - 1) {
            COMPOSE[COMPOSE_N++] = (char)k; COMPOSE[COMPOSE_N] = 0;
        }
    }
}

/* ---- generation hooks -------------------------------------------------------------------------- */
static app_turn *pending;

void app_begin_turn(const char *question) {
    app_chat *c;
    if (CUR < 0) {
        char t[64]; snprintf(t, sizeof t, "%s", question);
        c = new_chat(t);
    } else c = &CHATS[CUR];
    if (c->nturns >= MAX_TURNS) return;
    pending = &c->turn[c->nturns++];
    memset(pending, 0, sizeof *pending);
    snprintf(pending->q, sizeof pending->q, "%s", question);
    BUSY = 1;
}
void app_stream_token(const char *piece) {
    if (!pending) return;
    int n = (int)strlen(pending->a), m = (int)strlen(piece);
    if (n + m < (int)sizeof pending->a - 1) { memcpy(pending->a + n, piece, (size_t)m); pending->a[n + m] = 0; }
}
int app_history(const char **q, const char **a, int max) {
    if (CUR < 0) return 0;
    app_chat *c = &CHATS[CUR];
    int n = 0;
    /* exclude the turn currently being generated -- it has no answer yet */
    int upto = c->nturns - (pending ? 1 : 0);
    for (int i = 0; i < upto && n < max; i++) {
        if (!c->turn[i].q[0]) continue;
        q[n] = c->turn[i].q; a[n] = c->turn[i].a; n++;
    }
    return n;
}

void app_set_summary(const char *s) {
    if (pending && s) snprintf(pending->sum, sizeof pending->sum, "%s", s);
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

void app_stream_end(void) { if (pending) pending->done = 1; pending = 0; BUSY = 0; SCROLL = 1 << 20; }

/* Clamp the scroll to the measured content height. Called from draw, because the height is only
 * known once the answer text has been laid out -- it changes on every streamed token. */
static void clamp_scroll(int content_h, int view_h) {
    int max = content_h - view_h;
    if (max < 0) max = 0;
    if (SCROLL > max) SCROLL = max;
    if (SCROLL < 0) SCROLL = 0;
}
