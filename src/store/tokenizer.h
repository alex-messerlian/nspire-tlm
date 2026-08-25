#ifndef NS_TOKENIZER_H
#define NS_TOKENIZER_H
#define NS_TOK_MAX_LEN   64          /* longest vocab entry in bytes */
#define NS_MAX_TOKENS  1024
enum { NST_OK=0, NST_ERR_ARG=-1, NST_ERR_OPEN=-2, NST_ERR_MEM=-3, NST_ERR_MAGIC=-4,
       NST_ERR_PARSE=-5, NST_ERR_TRUNC=-6, NST_ERR_OVERFLOW=-7 };
typedef struct {
    char  *slab;
    struct { const unsigned char *b; int len; int id; } *vocab;   int nvocab;
    struct { const unsigned char *a; int alen; const unsigned char *b; int blen; } *merge; int nmerge;
    struct { const char *s; int len; int id; } *special;          int nspecial;
} ns_tok;
int  ns_tok_load(ns_tok *t, const char *path);
void ns_tok_free(ns_tok *t);
/* Returns token count, or a negative NST_ERR_*. Never partially fills on error. */
int  ns_tok_encode(const ns_tok *t, const char *text, int *out, int max);
int  ns_tok_decode(const ns_tok *t, const int *ids, int n, char *out, int cap);
const char *ns_tok_strerror(int code);
#endif
