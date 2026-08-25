#include <string.h>
#include <stdio.h>
#include "toolrun.h"
#include "eval.h"

/* Last occurrence of `needle`. */
static const char *rfind(const char *hay, const char *needle) {
    const char *last = 0, *p = hay;
    for (;;) { p = strstr(p, needle); if (!p) break; last = p; p++; }
    return last;
}

int tlm_extract_call(const char *doc, char *out, int cap) {
    out[0] = 0;
    if (!doc || cap < 2) return 0;
    const char *open = rfind(doc, "<tool>");
    if (!open) return 0;
    const char *close = strstr(open, "</tool>");
    if (!close) return 0;                       /* still streaming: not a complete call yet */
    int len = (int)(close - open) + 7;
    if (len >= cap) len = cap - 1;
    memcpy(out, open, (size_t)len);
    out[len] = 0;
    return len;
}

int tlm_result_span(const char *span, char *out, int cap) {
    char res[MAX_RESULT];
    res[0] = 0;
    int ok = 0;
    if (span && span[0] && tool_call_text(span, res, sizeof res) == TB_OK && res[0] && res[0] != '!')
        ok = 1;
    if (!res[0]) snprintf(res, sizeof res, "!give");
    snprintf(out, (size_t)cap, "<res>%s</res>", res);
    return ok;
}

void tlm_call_label(const char *span, char *out, int cap) {
    out[0] = 0;
    if (!span || cap < 4) return;
    const char *p = strstr(span, "<tool>");
    if (!p) { snprintf(out, (size_t)cap, "eval"); return; }
    p += 6;
    const char *a = strstr(p, "<arg>");
    const char *e = strstr(p, "</tool>");
    if (!e) { snprintf(out, (size_t)cap, "eval"); return; }
    int o = 0;
    for (const char *q = p; q < (a && a < e ? a : e) && o < cap - 3; q++)
        if (*q != ' ') out[o++] = *q;
    if (!a || a >= e) { out[o] = 0; return; }
    out[o++] = '(';
    for (const char *q = a; q < e && o < cap - 2; ) {
        if (!strncmp(q, "<arg>", 5)) {
            if (o > 0 && out[o-1] != '(') { out[o++] = ','; if (o < cap-2) out[o++] = ' '; }
            q += 5; continue;
        }
        out[o++] = *q++;
    }
    if (o < cap - 1) out[o++] = ')';
    out[o] = 0;
}
