/* parser.c -- lexer + recursive descent parser for the grammar in TOOL_SPEC.md section 4.
 *
 * Every descent carries a depth counter. A 45M model will occasionally emit "((((((..." and the
 * parser must return E_PARSE rather than blowing the stack -- on the device there is no stack guard
 * and no way to recover.
 */
#include "eval.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *FUNCS[] = {
    "sin","cos","tan","asin","acos","atan","sinh","cosh","tanh",
    "exp","ln","log","log10","sqrt","abs","floor","ceil","round","min","max", NULL
};

/* NOTE: `log` is base 10, `ln` is natural. Textbook convention for the physics and chemistry
 * material in the corpus. Stated here because getting this backwards is silent and catastrophic. */

int is_func_name(const char *s) {
    for (int i = 0; FUNCS[i]; i++) if (strcmp(FUNCS[i], s) == 0) return 1;
    return 0;
}

typedef struct {
    const char *s;
    size_t      p;
    arena_t    *a;
    int         allow_eq;
    int         bars;      /* depth of open |...| groups; see starts_primary */
    err_t       err;
} P;

static void skip_ws(P *p) { while (p->s[p->p] == ' ' || p->s[p->p] == '\t') p->p++; }
static char peek(P *p)    { skip_ws(p); return p->s[p->p]; }

static node_t *parse_add(P *p, int depth);

/* A number literal. 'e' starts an exponent only when a digit follows (optionally after a sign);
 * otherwise it is the constant e and the literal ends here. TOOL_SPEC.md section 4. */
static node_t *parse_number(P *p) {
    size_t start = p->p;
    while (isdigit((unsigned char)p->s[p->p])) p->p++;
    if (p->s[p->p] == '.') { p->p++; while (isdigit((unsigned char)p->s[p->p])) p->p++; }
    if (p->s[p->p] == 'e' || p->s[p->p] == 'E') {
        size_t save = p->p;
        p->p++;
        if (p->s[p->p] == '+' || p->s[p->p] == '-') p->p++;
        if (isdigit((unsigned char)p->s[p->p])) { while (isdigit((unsigned char)p->s[p->p])) p->p++; }
        else p->p = save;
    }
    char buf[64];
    size_t n = p->p - start;
    if (n == 0 || n >= sizeof buf) { p->err = E_PARSE; return NULL; }
    memcpy(buf, p->s + start, n); buf[n] = 0;
    return ar_num(p->a, strtod(buf, NULL));
}

static int read_ident(P *p, char *out) {
    size_t k = 0;
    while (isalnum((unsigned char)p->s[p->p]) || p->s[p->p] == '_') {
        if (k >= MAX_IDENT - 1) { p->err = E_PARSE; return 0; }
        out[k++] = p->s[p->p++];
    }
    out[k] = 0;
    return k > 0;
}

static node_t *parse_primary(P *p, int depth) {
    if (depth > MAX_DEPTH) { p->err = E_PARSE; return NULL; }
    char c = peek(p);

    if (c == '(') {
        p->p++;
        node_t *e = parse_add(p, depth + 1);
        if (!e) return NULL;
        if (peek(p) != ')') { p->err = E_PARSE; return NULL; }
        p->p++;
        return e;
    }
    if (c == '|') {
        p->p++;
        p->bars++;
        node_t *e = parse_add(p, depth + 1);
        if (!e) return NULL;
        if (peek(p) != '|') { p->err = E_PARSE; return NULL; }
        p->p++;
        p->bars--;
        node_t *n = ar_new(p->a, N_CALL);
        if (!n) { p->err = E_RANGE; return NULL; }
        strcpy(n->name, "abs"); n->kid[0] = e; n->nkid = 1;
        return n;
    }
    if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)p->s[p->p + 1]))) {
        skip_ws(p);
        return parse_number(p);
    }
    if (isalpha((unsigned char)c) || c == '_') {
        skip_ws(p);
        char name[MAX_IDENT];
        if (!read_ident(p, name)) { p->err = E_PARSE; return NULL; }
        /* IDENT '(' is a call only for known function names; otherwise it is implicit
         * multiplication, so x(y+1) means x*(y+1) as it does in textbooks. */
        if (peek(p) == '(' && is_func_name(name)) {
            p->p++;
            node_t *n = ar_new(p->a, N_CALL);
            if (!n) { p->err = E_RANGE; return NULL; }
            strncpy(n->name, name, MAX_IDENT - 1);
            for (;;) {
                if (n->nkid >= MAX_ARGS) { p->err = E_PARSE; return NULL; }
                node_t *arg = parse_add(p, depth + 1);
                if (!arg) return NULL;
                n->kid[n->nkid++] = arg;
                if (peek(p) == ',') { p->p++; continue; }
                break;
            }
            if (peek(p) != ')') { p->err = E_PARSE; return NULL; }
            p->p++;
            return n;
        }
        return ar_sym(p->a, name);
    }
    p->err = E_PARSE;
    return NULL;
}

static node_t *parse_postfix(P *p, int depth) {
    node_t *n = parse_primary(p, depth);
    if (!n) return NULL;
    while (peek(p) == '!') {
        p->p++;
        node_t *f = ar_new(p->a, N_CALL);
        if (!f) { p->err = E_RANGE; return NULL; }
        strcpy(f->name, "fact"); f->kid[0] = n; f->nkid = 1;
        n = f;
    }
    return n;
}

static node_t *parse_unary(P *p, int depth);

/* Right associative, and the exponent may itself be unary so 2^-3 works. */
static node_t *parse_power(P *p, int depth) {
    node_t *base = parse_postfix(p, depth);
    if (!base) return NULL;
    if (peek(p) == '^') {
        p->p++;
        node_t *ex = parse_unary(p, depth + 1);
        if (!ex) return NULL;
        return ar_bin(p->a, N_POW, base, ex);
    }
    return base;
}

static node_t *parse_unary(P *p, int depth) {
    if (depth > MAX_DEPTH) { p->err = E_PARSE; return NULL; }
    char c = peek(p);
    if (c == '-') {
        p->p++;
        node_t *e = parse_unary(p, depth + 1);
        if (!e) return NULL;
        node_t *n = ar_new(p->a, N_NEG);
        if (!n) { p->err = E_RANGE; return NULL; }
        n->kid[0] = e; n->nkid = 1;
        return n;
    }
    if (c == '+') { p->p++; return parse_unary(p, depth + 1); }
    return parse_power(p, depth);
}

/* True when the next token could begin a primary, i.e. implicit multiplication applies.
 *
 * '|' is the one genuinely ambiguous character in the grammar: it both opens and closes an absolute
 * value, so inside a bar group a '|' must be read as the CLOSER, not as the start of a new factor.
 * Without the p->bars guard, "|3-7|" parses the trailing bar as implicit multiplication by a new
 * bar group and then hits end of input. */
static int starts_primary(P *p) {
    char c = peek(p);
    if (c == '|') return p->bars == 0;
    return isdigit((unsigned char)c) || isalpha((unsigned char)c) || c == '_' || c == '(';
}

/* IMPLICIT multiplication binds TIGHTER than explicit '*' and '/'.
 *
 * This is a deliberate departure from strict left-to-right, and it exists for units. With equal
 * precedence, "100 m/10 s" parses as ((100*m)/10)*s = 10 m*s -- arithmetically defensible and
 * physically nonsense. Every quantity written the natural way (m/s, km/h, N/m^2) came out with an
 * inverted denominator, which would have put wrong units through the entire physics corpus.
 *
 * Binding implicit multiplication tighter gives "100 m/10 s" -> (100*m)/(10*s) = 10 m/s, which is
 * what the notation means to a physicist and what every CAS does with "a/bc".
 *
 * KNOWN COST, accepted: "1/2 x" now means 1/(2*x), not (1/2)*x. In a physics corpus that form is
 * rare -- coefficients are written 0.5x or x/2 -- and unit correctness is worth far more.
 * Explicit operators are unaffected: "1/2*x" is still (1/2)*x. */
static node_t *parse_imul(P *p, int depth, int *nfactors) {
    node_t *l = parse_unary(p, depth);
    if (!l) return NULL;
    int n = 1;
    while (starts_primary(p)) {
        node_t *r = parse_unary(p, depth + 1);
        if (!r) return NULL;
        l = ar_bin(p->a, N_MUL, l, r);
        if (!l) { p->err = E_RANGE; return NULL; }
        n++;
    }
    if (nfactors) *nfactors = n;
    return l;
}

static node_t *parse_mul(P *p, int depth) {
    int dummy;
    node_t *l = parse_imul(p, depth, &dummy);
    if (!l) return NULL;
    for (;;) {
        char c = peek(p);
        if (c != '*' && c != '/') break;
        p->p++;
        int nf = 1;
        node_t *r = parse_imul(p, depth + 1, &nf);
        if (!r) return NULL;
        /* AMBIGUITY GUARD -- refuse rather than guess.
         *
         * A '/' followed by an implicit-multiplication group of 2+ factors has no agreed reading:
         *     100 m/10 s   a physicist means (100 m)/(10 s)   -> implicit binds TIGHTER
         *     1/2 m v^2    a physicist means (1/2)*m*v^2      -> implicit binds LOOSER
         * No single precedence satisfies both, and either choice silently produces a wrong answer
         * in the other case. So we reject, exactly as solve() returns !nosol rather than an
         * unreliable root. The generator emits explicit parentheses and is unaffected; a human
         * typing an ambiguous form gets !expr instead of a confident wrong number.
         *
         * Single-factor denominators are unambiguous and still work: m/s, km/h, 9.8 m/s^2. */
        if (c == '/' && nf > 1) { p->err = E_EXPR; return NULL; }
        l = ar_bin(p->a, c == '*' ? N_MUL : N_DIV, l, r);
        if (!l) { p->err = E_RANGE; return NULL; }
    }
    return l;
}

static node_t *parse_add(P *p, int depth) {
    if (depth > MAX_DEPTH) { p->err = E_PARSE; return NULL; }
    node_t *l = parse_mul(p, depth);
    if (!l) return NULL;
    for (;;) {
        char c = peek(p);
        if (c != '+' && c != '-') break;
        p->p++;
        node_t *r = parse_mul(p, depth + 1);
        if (!r) return NULL;
        l = ar_bin(p->a, c == '+' ? N_ADD : N_SUB, l, r);
        if (!l) { p->err = E_RANGE; return NULL; }
    }
    return l;
}

err_t parse_expr(arena_t *a, const char *src, int allow_eq, node_t **out) {
    if (!src || strlen(src) >= MAX_ARG_BYTES) return E_PARSE;
    for (const char *q = src; *q; q++)
        if ((unsigned char)*q < 0x20 || (unsigned char)*q > 0x7E) return E_PARSE;

    P p = { src, 0, a, allow_eq, 0, E_NONE };
    node_t *n = parse_add(&p, 0);
    if (!n) return p.err ? p.err : E_PARSE;

    if (peek(&p) == '=') {
        if (!allow_eq) return E_PARSE;
        p.p++;
        node_t *r = parse_add(&p, 0);
        if (!r) return p.err ? p.err : E_PARSE;
        n = ar_bin(a, N_EQ, n, r);
        if (!n) return E_RANGE;
    }
    if (peek(&p) != 0) return E_PARSE;      /* trailing junk is an error, not something to ignore */
    *out = n;
    return E_NONE;
}
