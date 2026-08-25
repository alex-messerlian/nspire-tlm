/* Device: load the flat .store file. No JSON, no malloc churn -- one slab, pointers into it.
 *
 * PORT LESSONS APPLIED (the project log, five device cycles lost to their absence):
 *   - every failure is a DISTINCT negative return code, never 0-records-and-success. A silently
 *     empty load reading as success is the exact failure this file is written against.
 *   - the trailing "END<TAB>count" is verified, so a TRUNCATED read cannot pass.
 *   - no printf in the load path: it is unrouted in the engine and would produce "no output",
 *     which is indistinguishable from a hang.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loader.h"

#define NS_MAGIC "NSTORE1"

static char *next_line(char **p) {
    char *s = *p;
    if (!s || !*s) return NULL;
    char *e = strchr(s, '\n');
    if (e) { *e = '\0'; *p = e + 1; } else { *p = s + strlen(s); }
    return s;
}
static char *next_field(char **p) {
    char *s = *p;
    if (!s) return NULL;
    char *e = strchr(s, '\t');
    if (e) { *e = '\0'; *p = e + 1; } else { *p = NULL; }
    return s;
}

int ns_load(ns_store2 *st, const char *path) {
    if (!st || !path) return NS_ERR_ARG;
    memset(st, 0, sizeof *st);
    FILE *fp = fopen(path, "rb");
    if (!fp) return NS_ERR_OPEN;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NS_ERR_SEEK; }
    long n = ftell(fp);
    if (n <= 0) { fclose(fp); return NS_ERR_EMPTY_FILE; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NS_ERR_SEEK; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(fp); return NS_ERR_MEM; }
    size_t got = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    if (got != (size_t)n) { free(buf); return NS_ERR_SHORT_READ; }
    buf[n] = '\0';
    st->slab = buf;

    char *p = buf, *line;
    line = next_line(&p);
    if (!line || strcmp(line, NS_MAGIC) != 0) { free(buf); st->slab = 0; return NS_ERR_MAGIC; }
    line = next_line(&p);
    if (!line) { free(buf); st->slab = 0; return NS_ERR_TRUNC; }
    int declared = atoi(line);
    if (declared <= 0 || declared > NS_MAX_RECORDS) { free(buf); st->slab = 0; return NS_ERR_COUNT; }

    st->rec = (ns_rec2 *)calloc((size_t)declared, sizeof(ns_rec2));
    if (!st->rec) { free(buf); st->slab = 0; return NS_ERR_MEM; }

    int i = 0;
    while ((line = next_line(&p)) != NULL) {
        char *q = line, *tag = next_field(&q);
        if (!tag) { ns_free(st); return NS_ERR_PARSE; }
        if (strcmp(tag, "END") == 0) {
            char *cnt = next_field(&q);
            /* The end marker must agree with the header AND with what was actually read.
             * Without this a truncated file yields a short store and returns success. */
            if (!cnt || atoi(cnt) != declared || i != declared) { ns_free(st); return NS_ERR_TRUNC; }
            st->n = i;
            return NS_OK;
        }
        if (strcmp(tag, "R") != 0) { ns_free(st); return NS_ERR_PARSE; }
        if (i >= declared) { ns_free(st); return NS_ERR_COUNT; }
        ns_rec2 *r = &st->rec[i];
        r->rid = next_field(&q); r->lhs = next_field(&q); r->formula = next_field(&q);
        r->name = next_field(&q); r->req = next_field(&q);
        char *nv = next_field(&q);
        if (!r->rid || !r->lhs || !r->formula || !r->name || !r->req || !nv ||
            !r->rid[0] || !r->lhs[0] || !r->formula[0] || !r->name[0] || !nv[0]) {
            ns_free(st); return NS_ERR_PARSE;      /* same empty-is-not-present rule */
        }
        r->nvars = atoi(nv);
        if (r->nvars < 0 || r->nvars > NS_MAX_VARS2) { ns_free(st); return NS_ERR_PARSE; }
        for (int k = 0; k < r->nvars; k++) {
            char *vl = next_line(&p);
            if (!vl) { ns_free(st); return NS_ERR_TRUNC; }
            char *vq = vl, *vt = next_field(&vq);
            if (!vt || strcmp(vt, "V") != 0) { ns_free(st); return NS_ERR_PARSE; }
            r->var[k]  = next_field(&vq);
            r->unit[k] = next_field(&vq);
            r->cval[k] = next_field(&vq);          /* may be "" -- absence is explicit */
            if (!r->cval[k]) r->cval[k] = "";      /* normalise: never hand a NULL downstream */
            /* EMPTY IS NOT PRESENT. next_field returns "" for a dropped field, which is a
             * non-NULL pointer and passed a bare NULL check -- found by the mutation pass, not
             * by the suite: a variable line that lost its unit loaded clean. */
            if (!r->var[k] || !r->unit[k] || !r->var[k][0] || !r->unit[k][0]) {
                ns_free(st); return NS_ERR_PARSE;
            }
        }
        i++;
    }
    ns_free(st);
    return NS_ERR_TRUNC;                            /* ran out of input before END */
}

void ns_free(ns_store2 *st) {
    if (!st) return;
    free(st->rec); free(st->slab);
    memset(st, 0, sizeof *st);
}

const char *ns_strerror(int code) {
    switch (code) {
    case NS_OK:              return "ok";
    case NS_ERR_ARG:         return "null argument";
    case NS_ERR_OPEN:        return "cannot open file";
    case NS_ERR_SEEK:        return "seek failed";
    case NS_ERR_EMPTY_FILE:  return "file is empty";
    case NS_ERR_MEM:         return "out of memory";
    case NS_ERR_SHORT_READ:  return "short read";
    case NS_ERR_MAGIC:       return "bad magic";
    case NS_ERR_COUNT:       return "bad record count";
    case NS_ERR_TRUNC:       return "truncated";
    case NS_ERR_PARSE:       return "parse error";
    default:                 return "unknown error";
    }
}
