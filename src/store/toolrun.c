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
