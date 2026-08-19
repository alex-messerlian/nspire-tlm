/* ast.c -- arena allocation for AST nodes.
 *
 * Bump allocator, reset per tool call. No free(), so no leaks and no fragmentation -- both of which
 * matter more on the device than on the host. Exhaustion returns NULL and the caller turns that into
 * E_RANGE rather than crashing.
 */
#include "eval.h"
#include <string.h>

void ar_reset(arena_t *a) { a->n = 0; }

node_t *ar_new(arena_t *a, ntype_t t) {
    if (a->n >= MAX_NODES) return NULL;
    node_t *n = &a->pool[a->n++];
    memset(n, 0, sizeof *n);
    n->t = t;
    return n;
}

node_t *ar_num(arena_t *a, double v) {
    node_t *n = ar_new(a, N_NUM);
    if (n) n->num = v;
    return n;
}

node_t *ar_sym(arena_t *a, const char *name) {
    node_t *n = ar_new(a, N_SYM);
    if (!n) return NULL;
    strncpy(n->name, name, MAX_IDENT - 1);
    n->name[MAX_IDENT - 1] = 0;
    return n;
}

node_t *ar_bin(arena_t *a, ntype_t t, node_t *l, node_t *r) {
    if (!l || !r) return NULL;
    node_t *n = ar_new(a, t);
    if (!n) return NULL;
    n->kid[0] = l; n->kid[1] = r; n->nkid = 2;
    return n;
}

node_t *ar_clone(arena_t *a, const node_t *s) {
    if (!s) return NULL;
    node_t *n = ar_new(a, s->t);
    if (!n) return NULL;
    n->num = s->num;
    memcpy(n->name, s->name, MAX_IDENT);
    n->nkid = s->nkid;
    for (int i = 0; i < s->nkid; i++) {
        n->kid[i] = ar_clone(a, s->kid[i]);
        if (!n->kid[i]) return NULL;
    }
    return n;
}
