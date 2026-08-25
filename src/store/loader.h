#ifndef NS_LOADER_H
#define NS_LOADER_H
#define NS_MAX_RECORDS 4096
#define NS_MAX_VARS2   8

/* Distinct codes, never a bare 0/-1: "loaded nothing" and "loaded fine" must not share a value. */
enum { NS_OK = 0, NS_ERR_ARG = -1, NS_ERR_OPEN = -2, NS_ERR_SEEK = -3, NS_ERR_EMPTY_FILE = -4,
       NS_ERR_MEM = -5, NS_ERR_SHORT_READ = -6, NS_ERR_MAGIC = -7, NS_ERR_COUNT = -8,
       NS_ERR_TRUNC = -9, NS_ERR_PARSE = -10 };

typedef struct {
    const char *rid, *lhs, *formula, *name, *req;
    const char *var[NS_MAX_VARS2], *unit[NS_MAX_VARS2], *cval[NS_MAX_VARS2];
    int nvars;
} ns_rec2;

typedef struct { ns_rec2 *rec; int n; char *slab; } ns_store2;

int  ns_load(ns_store2 *st, const char *path);
void ns_free(ns_store2 *st);
const char *ns_strerror(int code);
#endif
