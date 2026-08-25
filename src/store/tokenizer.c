/* ByteLevel BPE encoder for the device.
 *
 * The ByteLevel alphabet is decoded AT PACK TIME (tools/tok_pack.py), so this operates on raw
 * bytes and carries no unicode table. The store is ASCII by construction and the question is
 * keypad-typed, so the GPT-2 pre-tokenizer regex reduces to isalpha/isdigit -- no \p{L} needed.
 * Those two decisions together are what makes this ~200 lines instead of ~600.
 *
 * No printf anywhere: unrouted output on device is indistinguishable from a hang. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "tokenizer.h"

static char *nl(char **p) {
    char *s = *p; if (!s || !*s) return NULL;
    char *e = strchr(s, '\n');
    if (e) { *e = '\0'; *p = e + 1; } else *p = s + strlen(s);
    return s;
}
static int unhex1(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* decode hex IN PLACE into the slab; returns length or -1 */
static int unhex(char *s, unsigned char **out) {
    int n = (int)strlen(s);
    if (n & 1) return -1;
    unsigned char *d = (unsigned char *)s;
    for (int i = 0; i < n / 2; i++) {
        int hi = unhex1(s[2*i]), lo = unhex1(s[2*i+1]);
        if (hi < 0 || lo < 0) return -1;
        d[i] = (unsigned char)((hi << 4) | lo);
    }
    *out = d;
    return n / 2;
}

int ns_tok_load(ns_tok *t, const char *path) {
    if (!t || !path) return NST_ERR_ARG;
    memset(t, 0, sizeof *t);
    FILE *fp = fopen(path, "rb");
    if (!fp) return NST_ERR_OPEN;
    fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (n <= 0) { fclose(fp); return NST_ERR_TRUNC; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(fp); return NST_ERR_MEM; }
    size_t got = fread(buf, 1, (size_t)n, fp); fclose(fp);
    if (got != (size_t)n) { free(buf); return NST_ERR_TRUNC; }
    buf[n] = '\0'; t->slab = buf;
    char *p = buf, *line = nl(&p);
    if (!line || strcmp(line, "NSTOK1")) { ns_tok_free(t); return NST_ERR_MAGIC; }
    line = nl(&p);
    int ns = 0, nv = 0, nm = 0;
    if (!line || sscanf(line, "%d %d %d", &ns, &nv, &nm) != 3) { ns_tok_free(t); return NST_ERR_PARSE; }
    if (ns <= 0 || nv <= 0 || nm < 0) { ns_tok_free(t); return NST_ERR_PARSE; }
    t->vocab   = calloc((size_t)nv, sizeof *t->vocab);
    t->merge   = calloc((size_t)(nm ? nm : 1), sizeof *t->merge);
    t->special = calloc((size_t)ns, sizeof *t->special);
    if (!t->vocab || !t->merge || !t->special) { ns_tok_free(t); return NST_ERR_MEM; }
    while ((line = nl(&p)) != NULL) {
        if (!strncmp(line, "END ", 4)) {
            if (atoi(line + 4) != nv || t->nvocab + t->nspecial != nv) { ns_tok_free(t); return NST_ERR_TRUNC; }
            return NST_OK;
        }
        char tag = line[0];
        char *rest = line + 2;
        if (tag == 'S') {
            char *sp = strchr(rest, ' ');
            if (!sp) { ns_tok_free(t); return NST_ERR_PARSE; }
            *sp = '\0';
            t->special[t->nspecial].id  = atoi(rest);
            t->special[t->nspecial].s   = sp + 1;
            t->special[t->nspecial].len = (int)strlen(sp + 1);
            t->nspecial++;
        } else if (tag == 'V') {
            char *sp = strchr(rest, ' ');
            if (!sp) { ns_tok_free(t); return NST_ERR_PARSE; }
            *sp = '\0';
            unsigned char *b; int len = unhex(sp + 1, &b);
            if (len < 0) { ns_tok_free(t); return NST_ERR_PARSE; }
            t->vocab[t->nvocab].id = atoi(rest);
            t->vocab[t->nvocab].b = b; t->vocab[t->nvocab].len = len;
            t->nvocab++;
        } else if (tag == 'M') {
            char *s1 = strchr(rest, ' '); if (!s1) { ns_tok_free(t); return NST_ERR_PARSE; }
            *s1 = '\0';
            char *s2 = strchr(s1 + 1, ' '); if (!s2) { ns_tok_free(t); return NST_ERR_PARSE; }
            *s2 = '\0';
            unsigned char *a, *b; int al = unhex(s1 + 1, &a), bl = unhex(s2 + 1, &b);
            if (al < 0 || bl < 0) { ns_tok_free(t); return NST_ERR_PARSE; }
            /* merge_rank() uses the ARRAY INDEX as priority, so the rank column is only
             * meaningful if the file is in rank order. Verify it, or a reordered file
             * mis-tokenizes SILENTLY -- corrupting a rank passed the reproducer 581/581,
             * found by the mutation pass and not by the suite. */
            if (atoi(rest) != t->nmerge) { ns_tok_free(t); return NST_ERR_PARSE; }
            t->merge[t->nmerge].a = a; t->merge[t->nmerge].alen = al;
            t->merge[t->nmerge].b = b; t->merge[t->nmerge].blen = bl;
            t->nmerge++;
        } else { ns_tok_free(t); return NST_ERR_PARSE; }
    }
    ns_tok_free(t);
    return NST_ERR_TRUNC;
}

void ns_tok_free(ns_tok *t) {
    if (!t) return;
    free(t->vocab); free(t->merge); free(t->special); free(t->slab);
    memset(t, 0, sizeof *t);
}

static int lookup(const ns_tok *t, const unsigned char *b, int len) {
    for (int i = 0; i < t->nvocab; i++)
        if (t->vocab[i].len == len && !memcmp(t->vocab[i].b, b, (size_t)len)) return t->vocab[i].id;
    return -1;
}
static int merge_rank(const ns_tok *t, const unsigned char *a, int al,
                      const unsigned char *b, int bl) {
    for (int i = 0; i < t->nmerge; i++)
        if (t->merge[i].alen == al && t->merge[i].blen == bl &&
            !memcmp(t->merge[i].a, a, (size_t)al) && !memcmp(t->merge[i].b, b, (size_t)bl)) return i;
    return -1;
}

/* BPE one pre-token: start from single bytes, repeatedly apply the lowest-ranked merge. */
static int bpe(const ns_tok *t, const unsigned char *w, int wlen, int *out, int max) {
    if (wlen == 0) return 0;
    static unsigned char parts[NS_TOK_MAX_LEN * 4][NS_TOK_MAX_LEN];
    static int plen[NS_TOK_MAX_LEN * 4];
    int np = 0;
    for (int i = 0; i < wlen && np < NS_TOK_MAX_LEN * 4; i++) { parts[np][0] = w[i]; plen[np] = 1; np++; }
    for (;;) {
        int best = -1, bi = -1;
        for (int i = 0; i + 1 < np; i++) {
            int r = merge_rank(t, parts[i], plen[i], parts[i+1], plen[i+1]);
            if (r >= 0 && (best < 0 || r < best)) { best = r; bi = i; }
        }
        if (bi < 0) break;
        if (plen[bi] + plen[bi+1] > NS_TOK_MAX_LEN) break;
        memcpy(parts[bi] + plen[bi], parts[bi+1], (size_t)plen[bi+1]);
        plen[bi] += plen[bi+1];
        for (int i = bi + 1; i + 1 < np; i++) { memcpy(parts[i], parts[i+1], (size_t)plen[i+1]); plen[i] = plen[i+1]; }
        np--;
    }
    int n = 0;
    for (int i = 0; i < np; i++) {
        if (n >= max) return NST_ERR_OVERFLOW;
        int id = lookup(t, parts[i], plen[i]);
        if (id < 0) return NST_ERR_PARSE;          /* unknown byte: the vocab must cover all 256 */
        out[n++] = id;
    }
    return n;
}

/* GPT-2 pre-tokenizer, ASCII subset. Contractions, then ' ?letters', ' ?digits',
 * ' ?other', trailing whitespace. add_prefix_space=true prepends one space. */
static int is_l(int c) { return isalpha((unsigned char)c); }
static int is_d(int c) { return isdigit((unsigned char)c); }

/* Encode ONE non-special segment: prefix space, GPT-2 split, BPE each piece. */
static int encode_segment(const ns_tok *t, const char *seg, int seglen, int *out, int max) {
    static char buf[8192];
    if (seglen <= 0) return 0;
    if ((size_t)seglen + 2 > sizeof buf) return NST_ERR_OVERFLOW;
    /* add_prefix_space applies to EVERY segment, not once per input: HuggingFace splits on
     * added tokens FIRST and pre-tokenizes each remaining piece independently. Prepending only
     * at the start made "<q>A test" tokenize "A" where the reference has " A" -- 66 of 581
     * reference cases, every one of them an assembled prompt. */
    /* ...and only when the segment does not ALREADY begin with a space. Prepending
     * unconditionally doubled the leading whitespace token on 3 of 581 reference cases. */
    int pre = (seg[0] != ' ') ? 1 : 0;
    if (pre) buf[0] = ' ';
    memcpy(buf + pre, seg, (size_t)seglen);
    buf[seglen + pre] = '\0';
    const char *p = buf;
    int n = 0;
    while (*p) {
        const char *start = p;
        if (*p == '\'' && p[1]) {
            static const char *C[] = {"'s","'t","'re","'ve","'m","'ll","'d"};
            for (int i = 0; i < 7; i++)
                if (!strncmp(p, C[i], strlen(C[i]))) { p += strlen(C[i]); break; }
            if (p == start) p++;
        } else {
            const char *q = p;
            if (*q == ' ' && q[1] && !isspace((unsigned char)q[1])) q++;
            if (is_l(*q))      { while (is_l(*q)) q++; }
            else if (is_d(*q)) { while (is_d(*q)) q++; }
            else if (*q && !isspace((unsigned char)*q)) {
                while (*q && !isspace((unsigned char)*q) && !is_l(*q) && !is_d(*q)) q++;
            } else {
                const char *w = p; while (isspace((unsigned char)*w)) w++;
                q = (*w) ? w - 1 : w;
                if (q == p) q = w;
            }
            p = (q > p) ? q : p + 1;
        }
        int r = bpe(t, (const unsigned char *)start, (int)(p - start), out + n, max - n);
        if (r < 0) return r;
        n += r;
    }
    return n;
}

int ns_tok_encode(const ns_tok *t, const char *text, int *out, int max) {
    if (!t || !text || !out || max <= 0) return NST_ERR_ARG;
    int n = 0;
    const char *p = text, *seg = text;
    while (*p) {
        int hit = -1, hlen = 0;
        for (int i = 0; i < t->nspecial; i++)
            if (!strncmp(p, t->special[i].s, (size_t)t->special[i].len)) {
                hit = t->special[i].id; hlen = t->special[i].len; break;
            }
        if (hit < 0) { p++; continue; }
        int r = encode_segment(t, seg, (int)(p - seg), out + n, max - n);
        if (r < 0) return r;
        n += r;
        if (n >= max) return NST_ERR_OVERFLOW;
        out[n++] = hit;
        p += hlen; seg = p;
    }
    int r = encode_segment(t, seg, (int)(p - seg), out + n, max - n);
    if (r < 0) return r;
    return n + r;
}

/* Decode: concatenate the raw bytes of each id. Specials print as their literal text so a
 * <tool> in the output is visible rather than silently dropped. Returns bytes written. */
int ns_tok_decode(const ns_tok *t, const int *ids, int n, char *out, int cap) {
    if (!t || !ids || !out || cap <= 0) return -1;
    int w = 0;
    for (int i = 0; i < n; i++) {
        const unsigned char *b = 0; int len = 0;
        for (int k = 0; k < t->nspecial; k++)
            if (t->special[k].id == ids[i]) { b = (const unsigned char *)t->special[k].s;
                                              len = t->special[k].len; break; }
        if (!b) for (int k = 0; k < t->nvocab; k++)
            if (t->vocab[k].id == ids[i]) { b = t->vocab[k].b; len = t->vocab[k].len; break; }
        if (!b) continue;
        if (w + len >= cap) break;
        memcpy(out + w, b, (size_t)len); w += len;
    }
    out[w] = 0;
    return w;
}

const char *ns_tok_strerror(int c) {
    switch (c) {
    case NST_OK: return "ok";
    case NST_ERR_ARG: return "bad argument";
    case NST_ERR_OPEN: return "cannot open";
    case NST_ERR_MEM: return "out of memory";
    case NST_ERR_MAGIC: return "bad magic";
    case NST_ERR_PARSE: return "parse error";
    case NST_ERR_TRUNC: return "truncated";
    case NST_ERR_OVERFLOW: return "token buffer overflow";
    default: return "unknown";
    }
}
