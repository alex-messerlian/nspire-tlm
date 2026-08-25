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
static int CHAT_SCROLL;              /* index of the first chat row drawn */
static int CHAT_AT[MAX_CHATS];       /* screen row -> chat index */
static int CHAT_FIT;                 /* rows that fit, recomputed each frame */
static gfx_rect R_LIST;              /* the scrollable list area, for hit-testing the wheel */

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
static int draw_answer(int x, int y, int w, const char *raw, int draw) {
    int lh = gfx_font_h(F_UI) + 2, cy = y;
    const char *p = raw;
    char buf[512]; sp_kind k;
    while ((p = span_next(p, &k, buf, sizeof buf)) != 0) {
        if (k == SP_TEXT) {
            const char *s = buf; while (*s == ' ') s++;
            if (*s) {
                int n = gfx_text_wrap(x, cy, s, F_UI, C_INK, C_BG, w, lh, draw);
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
static void magnifier(int x, int y, uint16_t c) {
    const int R = 4;
    for (int j = -R; j <= R; j++)
        for (int i = -R; i <= R; i++) {
            int d = i * i + j * j;
            if (d <= R * R && d > (R - 2) * (R - 2)) gfx_fill(x + R + i, y + R + j, 1, 1, c);
        }
    for (int k = 0; k < 4; k++) gfx_fill(x + 7 + k, y + 7 + k, 2, 1, c);   /* handle */
}

/* compose glyph for New chat: the web row has one and the device row did not, so the two rows sat
 * on different text baselines. */
static void pencil(int x, int y, uint16_t c) {
    for (int k = 0; k < 7; k++) gfx_fill(x + 2 + k, y + 8 - k, 2, 1, c);   /* shaft */
    gfx_fill(x + 1, y + 8, 2, 2, c);                                       /* tip  */
    gfx_fill(x + 8, y + 1, 2, 2, c);                                       /* eraser */
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
    gfx_dim(HEX(0x000000), 28);            /* same scrim weight as the web popup */

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
    gfx_rrect(SX, SY, SW, SH, 8, C_BG);

    /* field */
    gfx_hline(SX + 10, SY + 27, SW - 20, C_LINE);
    magnifier(SX + 11, SY + 8, C_INK3);
    if (SQ_N) gfx_text(SX + 28, SY + 7, SQ, F_UI, C_INK, C_BG);
    else      gfx_text(SX + 28, SY + 7, "Search chats...", F_UI, C_INK3, C_BG);
    if (SQ_N) gfx_vline(SX + 29 + gfx_text_w(SQ, F_UI), SY + 8, 12, C_INK);

    char terms[MAX_TERMS][TERM_MAX];
    int nt = split_terms(SQ, terms);

    if (!rows) {
        gfx_text(SX + 12, SY + 36, SQ_N ? "No chats match" : "No chats yet", F_UI, C_INK3, C_BG);
        return;
    }
    for (int i = 0; i < rows; i++) {
        int y = SY + 34 + i * 30;
        R_SROW[i] = (gfx_rect){ SX + 4, y - 2, SW - 8, 28 };
        int hit = SSCROLL + i;
        int hot = (hit == SSEL) || (HOVER && inside(R_SROW[i], MX, MY));
        uint16_t bg = hot ? C_SEL : C_BG;
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
        gfx_text(SX + 10, SY + 34 + rows * 30 - 1, more, F_UI, C_INK3, C_BG);
    }
}

static void draw_sidebar(void) {
    gfx_fill(0, 0, SIDE_W, GFX_H, C_SIDE);
    gfx_vline(SIDE_W, 0, GFX_H, C_LINE);

    /* header: toggle on the right, matching the main bar's height so both sit on one line */
    /* [search][toggle], right-aligned and the same size, as on the web header */
    R_TOGGLE = (gfx_rect){ SIDE_W - 21, 3, 18, 18 };
    R_SEARCH = (gfx_rect){ SIDE_W - 41, 3, 18, 18 };
    {   int sh = HOVER && inside(R_SEARCH, MX, MY);
        gfx_rrect(R_SEARCH.x, R_SEARCH.y, 18, 18, 4, sh ? C_SEL : C_SIDE);
        magnifier(R_SEARCH.x + 4, R_SEARCH.y + 4, C_INK2);
    }
    {   int th = HOVER && inside(R_TOGGLE, MX, MY);
        gfx_rrect(R_TOGGLE.x, R_TOGGLE.y, 18, 18, 4, th ? C_SEL : C_SIDE);
        gfx_rrect_outline(R_TOGGLE.x + 3, R_TOGGLE.y + 4, 13, 11, 2, C_INK2);
        gfx_vline(R_TOGGLE.x + 8, R_TOGGLE.y + 4, 11, C_INK2);
    }

    R_NEW = (gfx_rect){ 4, TOP_H + 2, SIDE_W - 8, 18 };
    {   int hot = HOVER && inside(R_NEW, MX, MY);
        uint16_t nb = hot ? C_SEL : C_SIDE;
        if (hot) gfx_rrect(R_NEW.x, R_NEW.y, R_NEW.w, R_NEW.h, 5, nb);
        pencil(R_NEW.x + 5, R_NEW.y + 3, C_INK2);
        gfx_text(R_NEW.x + 20, R_NEW.y + 2, "New chat", F_UIB, C_INK, nb);
    }

    gfx_text(10, TOP_H + 26, "Chats", F_UI, C_INK3, C_SIDE);

    /* The list SCROLLS. It used to break at the first row that did not fit, which silently hid up
     * to six of a twelve-session cap -- unreachable, with nothing on screen admitting it. Hiding
     * data is worse than truncating it, because truncation is visible. */
    {
        int top = TOP_H + 42, bot = GFX_H - DOCK_H - 6;
        CHAT_FIT = (bot - top) / 18;
        if (CHAT_FIT < 1) CHAT_FIT = 1;
        R_LIST = (gfx_rect){ 0, top - 4, SIDE_W, bot - top + 8 };

        int maxs = NCHATS - CHAT_FIT; if (maxs < 0) maxs = 0;
        if (CHAT_SCROLL > maxs) CHAT_SCROLL = maxs;
        if (CHAT_SCROLL < 0) CHAT_SCROLL = 0;

        int over = NCHATS > CHAT_FIT;
        int rowmax = over ? SIDE_W - 14 : SIDE_W - 8;   /* leave room for the bar when it shows */

        NCHAT_ROWS = 0;
        for (int r = 0; r < CHAT_FIT && CHAT_SCROLL + r < NCHATS; r++) {
            int i = CHAT_SCROLL + r, y = top + r * 18;
            CHAT_AT[r] = i;
            R_CHAT[r]  = (gfx_rect){ 4, y - 2, rowmax, 17 };
            R_TRASH[r] = (gfx_rect){ rowmax - 12, y - 1, 14, 14 };
            int hot = HOVER && inside(R_CHAT[r], MX, MY);
            uint16_t bg = (i == CUR || hot) ? C_SEL : C_SIDE;
            if (i == CUR || hot) gfx_rrect(R_CHAT[r].x, R_CHAT[r].y, R_CHAT[r].w, R_CHAT[r].h, 5, bg);
            gfx_text_ellipsis(10, y, CHATS[i].title, F_UI, C_INK, bg, rowmax - (hot ? 26 : 10));
            if (hot) {                    /* trash appears only on hover, as on the web */
                int tx = R_TRASH[r].x, ty = R_TRASH[r].y;
                uint16_t tb = inside(R_TRASH[r], MX, MY) ? HEX(0xE6E6E6) : bg;
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
        /* A scrollbar, so "there is more" is visible rather than inferred. */
        if (over) {
            int track_h = CHAT_FIT * 18, tx = SIDE_W - 7;
            int knob = track_h * CHAT_FIT / NCHATS; if (knob < 12) knob = 12;
            int ky = top - 2 + (track_h - knob) * CHAT_SCROLL / maxs;
            gfx_rrect(tx, top - 2, 3, track_h, 1, HEX(0xEDEDED));
            gfx_rrect(tx, ky, 3, knob, 1, C_INK3);
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

    /* top bar: ChatTLM when empty, the chat's title inside a chat */
    const char *title = (CUR >= 0) ? CHATS[CUR].title : "ChatTLM";
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

    gfx_text(x0 + (w - gfx_text_w("ChatTLM can make mistakes.", F_UI)) / 2, GFX_H - 14,
             "ChatTLM can make mistakes.", F_UI, C_INK3, C_BG);
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
    if (SEARCH_ON) draw_search();
    draw_cursor();
    gfx_present();
}

/* ---- input ------------------------------------------------------------------------------------ */
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
        if (inside(R_TOGGLE, MX, MY)) { SIDEBAR = !SIDEBAR; return; }
        if (inside(R_SEARCH, MX, MY)) {
            SEARCH_ON = 1; SQ_N = 0; SQ[0] = 0; SSEL = 0; run_search(); return;
        }
        if (SIDEBAR) {
            if (inside(R_NEW, MX, MY)) { CUR = -1; SCROLL = 0; COMPOSE_N = 0; COMPOSE[0] = 0; return; }
            for (int i = 0; i < NCHAT_ROWS; i++) {
                if (inside(R_TRASH[i], MX, MY)) { delete_chat(CHAT_AT[i]); return; }
                if (inside(R_CHAT[i], MX, MY))  { CUR = CHAT_AT[i]; SCROLL = 0; return; }
            }
        }
        if (inside(R_SEND, MX, MY) && COMPOSE_N && !BUSY) {
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
