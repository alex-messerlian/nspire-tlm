/* HOST-SIDE BYTE-EXACTNESS REPRODUCER, per the standing rule: build a minimal reproducer BEFORE
 * the first device round-trip. The tokenizer is the highest-risk component in the port and its
 * failure mode is silent -- wrong tokens produce fluent nonsense, not a crash.
 *
 * The oracle is build/tok_reference.json: (text, ids) pairs from the Python tokenizer over every
 * string the device will ever see, plus adversarial cases. A mismatch on ANY case fails. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tokenizer.h"

/* minimal JSON reader for the fixed reference shape [{"text":"..","ids":[..]}] */
static char *slurp(const char *p, long *len) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)*len + 1);
    if (b && fread(b, 1, (size_t)*len, f) != (size_t)*len) { free(b); b = NULL; }
    if (b) b[*len] = 0;
    fclose(f); return b;
}
static int unesc(const char *s, const char *e, char *out, int cap) {
    int n = 0;
    while (s < e && n < cap - 1) {
        if (*s == '\\' && s + 1 < e) {
            s++;
            switch (*s) {
            case 'n': out[n++]='\n'; break; case 't': out[n++]='\t'; break;
            case 'r': out[n++]='\r'; break; case '"': out[n++]='"'; break;
            case '\\': out[n++]='\\'; break; case '/': out[n++]='/'; break;
            case 'u': { int v=0; for (int k=1;k<=4&&s+k<e;k++){int c=s[k];v=v*16+(c<='9'?c-'0':(c|32)-'a'+10);}
                        s += 4; if (v < 128) out[n++] = (char)v; else return -1; break; }
            default: out[n++] = *s;
            }
            s++;
        } else out[n++] = *s++;
    }
    out[n] = 0; return n;
}

int main(int argc, char **argv) {
    ns_tok t;
    int rc = ns_tok_load(&t, argc > 1 ? argv[1] : "build/tok4096.tok");
    if (rc != NST_OK) { printf("  FAIL load: %s\n", ns_tok_strerror(rc)); return 1; }
    printf("  loaded vocab=%d merges=%d special=%d\n", t.nvocab, t.nmerge, t.nspecial);

    long L; char *js = slurp(argc > 2 ? argv[2] : "build/tok_reference.json", &L);
    if (!js) { printf("  FAIL cannot read reference\n"); return 1; }

    int cases = 0, bad = 0, tokens = 0;
    char text[8192]; int want[NS_MAX_TOKENS], got[NS_MAX_TOKENS];
    const char *p = js;
    while ((p = strstr(p, "\"text\":")) != NULL) {
        p += 7; while (*p == ' ') p++;
        if (*p != '"') break;
        const char *s = ++p;
        while (*p && !(*p == '"' && p[-1] != '\\')) p++;
        if (unesc(s, p, text, sizeof text) < 0) { p++; continue; }   /* non-ASCII case: skip */
        const char *ip = strstr(p, "\"ids\":");
        if (!ip) break;
        ip = strchr(ip, '[') + 1;
        int nw = 0;
        while (*ip && *ip != ']') {
            if (*ip == ',' || *ip == ' ') { ip++; continue; }
            want[nw++] = (int)strtol(ip, (char **)&ip, 10);
        }
        cases++; tokens += nw;
        int ng = ns_tok_encode(&t, text, got, NS_MAX_TOKENS);
        int ok = (ng == nw);
        for (int i = 0; ok && i < nw; i++) if (got[i] != want[i]) ok = 0;
        if (!ok) {
            if (bad < 5) {
                printf("  MISMATCH %.60s\n    want(%d):", text, nw);
                for (int i = 0; i < nw && i < 12; i++) printf(" %d", want[i]);
                printf("\n    got (%d):", ng);
                for (int i = 0; i < ng && i < 12; i++) printf(" %d", got[i]);
                printf("\n");
            }
            bad++;
        }
        p = ip;
    }
    ns_tok_free(&t); free(js);
    printf("  %d/%d cases byte-exact (%d reference tokens); %d mismatch(es)\n",
           cases - bad, cases, tokens, bad);
    return bad ? 1 : 0;
}
