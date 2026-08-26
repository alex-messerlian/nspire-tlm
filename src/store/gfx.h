#ifndef NS_GFX_H
#define NS_GFX_H
/* RGB565 framebuffer drawing for the 320x240 panel.
 *
 * The device is NOT limited to a character grid -- lcd_blit takes a full SCR_320x240_565 buffer,
 * so this draws real pixels: proportional anti-aliased text, rounded bubbles, icons, a cursor.
 * An earlier version of this UI used nspireio's console and I mistook that library's limits for
 * the hardware's.
 *
 * Everything renders into an off-screen buffer and is blitted once per frame. Drawing straight to
 * the panel tears, and on a 396 MHz core a torn frame during streaming is very visible.
 *
 * The same file builds on the host (TLM_HOST) against a plain malloc'd buffer, so the layout can
 * be iterated and screenshotted without a device round-trip. */
#include <stdint.h>

#define GFX_W 320
#define GFX_H 240

/* 8-8-8 in, RGB565 out. Colours are written as hex triples in the UI code so they can be checked
 * against the web design directly. */
#define RGB(r,g,b) ((uint16_t)((((r)&0xF8)<<8) | (((g)&0xFC)<<3) | ((b)&0xF8)>>3))
#define HEX(c)     RGB(((c)>>16)&0xFF, ((c)>>8)&0xFF, (c)&0xFF)

typedef struct { int x, y, w, h; } gfx_rect;

void      gfx_init(void);
void      gfx_free(void);
uint16_t *gfx_buf(void);
void      gfx_present(void);                       /* blit one finished frame */

void gfx_clear(uint16_t c);
void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_rrect(int x, int y, int w, int h, int r, uint16_t c);        /* filled, rounded */
void gfx_rrect_outline(int x, int y, int w, int h, int r, uint16_t c);
void gfx_hline(int x, int y, int w, uint16_t c);
void gfx_vline(int x, int y, int h, uint16_t c);

/* Clip rectangle: everything below is clipped to it. The transcript scrolls, so text has to stop
 * at the pane edge rather than painting over the composer. */
/* Blend the whole frame toward a colour, 0..100 percent. Modal scrim. */
void gfx_dim(uint16_t toward, int pct);

void gfx_clip(int x, int y, int w, int h);
void gfx_clip_reset(void);

/* ---- text ---------------------------------------------------------------------------------- */
/* F_SM is the session list's face. See tools/mkfont.py for why a fourth size exists. */
typedef enum { F_UI = 0, F_UIB = 1, F_BIG = 2, F_SM = 3, F_XS = 4 } gfx_font;

int  gfx_text(int x, int y, const char *utf8, gfx_font f, uint16_t fg, uint16_t bg);
int  gfx_text_w(const char *utf8, gfx_font f);     /* advance width, no drawing */
int  gfx_font_h(gfx_font f);
/* Draw at most `maxw` pixels, appending an ellipsis if it does not fit. Returns width drawn. */
int  gfx_text_ellipsis(int x, int y, const char *utf8, gfx_font f, uint16_t fg, uint16_t bg, int maxw);
/* Word-wrap: returns the number of lines that WOULD be drawn; draws them when draw is true. */
/* As gfx_text_wrap, but also reports the pixel width of the LAST line through `out_last_w`.
 * The composer needs it to put a caret after wrapped text: recomputing the break points at the
 * call site would be a second copy of the wrapping rule, and the two would drift. */
int  gfx_text_wrap_ex(int x, int y, const char *utf8, gfx_font f, uint16_t fg, uint16_t bg,
                      int maxw, int line_h, int draw, int *out_last_w);
int  gfx_text_wrap(int x, int y, const char *utf8, gfx_font f, uint16_t fg, uint16_t bg,
                   int maxw, int line_h, int draw);
#endif
