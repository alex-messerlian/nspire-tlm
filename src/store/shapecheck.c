/* shapecheck.c -- see shapecheck.h for why this exists and what it does not verify.
 *
 * Built on the SHIPPED parser (tools/eval/parser.c) and the shipped AST, not on a second expression
 * grammar. A validator with its own parser would disagree with the evaluator on exactly the inputs
 * where disagreement matters, and the repo already records three cases of a test's own heuristic
 * producing the failure rather than the code under test.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include "shapecheck.h"
#include "eval.h"

#define FLAT_MAX 16          /* operands in one flattened * or + chain; TOOL_SPEC caps args at 128 B */

/* ---- numbers ---------------------------------------------------------------------------------
 * Relative tolerance, not equality. Both sides reach here as decimal text through the same lexer,
 * so "2" and "2.0" already agree exactly; the tolerance absorbs the last-bit noise of a value that
 * made a round trip through the givens string. It is NOT a licence for "9.8" against "9.81" --
 * that is 1e-3 and fails. */
static int num_eq(double a, double b) {
    double m = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (m < 1e-300) return 1;
    return fabs(a - b) / m < 1e-9;
}

/* ---- a total order on canonicalised trees ----------------------------------------------------
 * Children are canonicalised before their parent sorts them, so this never has to re-sort. */
static int nodecmp(const node_t *a, const node_t *b) {
    if (a->t != b->t) return a->t < b->t ? -1 : 1;
    switch (a->t) {
        case N_NUM: if (num_eq(a->num, b->num)) return 0;
                    return a->num < b->num ? -1 : 1;
        case N_SYM: return strcmp(a->name, b->name);
        case N_CALL: { int c = strcmp(a->name, b->name); if (c) return c; break; }
        default: break;
    }
    if (a->nkid != b->nkid) return a->nkid < b->nkid ? -1 : 1;
    for (int i = 0; i < a->nkid; i++) { int c = nodecmp(a->kid[i], b->kid[i]); if (c) return c; }
    return 0;
}

/* ---- canonicalise ----------------------------------------------------------------------------
 * MUL and ADD: flatten the chain, canonicalise each operand, sort, rebuild RIGHT-ASSOCIATED. That
 * normalises associativity and commutativity together, and it fits the binary AST -- node_t caps
 * kid[] at MAX_ARGS (4), so an n-ary node is not available.
 *
 * Everything else keeps its operand order, deliberately. `/`, `-` and `^` are not commutative and
 * the whole value of this check is that it notices. */
/* sc_ prefix throughout: tools/eval/eval.h already declares canon(), the derivative
 * simplifier's sum-term ordering. A different operation with the same name in the same
 * translation unit is a shadow waiting to be introduced. */
static node_t *sc_canon(arena_t *ar, const node_t *n);

static void flatten(const node_t *n, ntype_t t, const node_t **out, int *cnt) {
    if (n->t == t && n->nkid == 2 && *cnt + 2 <= FLAT_MAX) {
        flatten(n->kid[0], t, out, cnt);
        flatten(n->kid[1], t, out, cnt);
        return;
    }
    if (*cnt < FLAT_MAX) out[(*cnt)++] = n;
}

static node_t *sc_canon(arena_t *ar, const node_t *n) {
    if (!n) return 0;
    if (n->t == N_MUL || n->t == N_ADD) {
        const node_t *raw[FLAT_MAX]; int cnt = 0;
        flatten(n, n->t, raw, &cnt);
        node_t *ch[FLAT_MAX];
        for (int i = 0; i < cnt; i++) { ch[i] = sc_canon(ar, raw[i]); if (!ch[i]) return 0; }
        for (int i = 1; i < cnt; i++) {            /* insertion sort: cnt <= 16 */
            node_t *k = ch[i]; int j = i - 1;
            while (j >= 0 && nodecmp(ch[j], k) > 0) { ch[j+1] = ch[j]; j--; }
            ch[j+1] = k;
        }
        node_t *acc = ch[cnt-1];
        for (int i = cnt - 2; i >= 0; i--) {
            node_t *p = ar_bin(ar, n->t, ch[i], acc);
            if (!p) return 0;
            acc = p;
        }
        return acc;
    }
    node_t *c = ar_new(ar, n->t);
    if (!c) return 0;
    c->num = n->num;
    memcpy(c->name, n->name, sizeof c->name);
    c->nkid = n->nkid;
    for (int i = 0; i < n->nkid; i++) { c->kid[i] = sc_canon(ar, n->kid[i]); if (!c->kid[i]) return 0; }
    return c;
}

/* ---- substitution ---------------------------------------------------------------------------- */
static node_t *subst(arena_t *ar, const node_t *n,
                     const char *const *var, const char *const *val, int nvals, int *unbound) {
    if (!n) return 0;
    if (n->t == N_SYM) {
        for (int i = 0; i < nvals; i++) {
            if (var[i] && val[i] && val[i][0] && strcmp(var[i], n->name) == 0) {
                char *end; double v = strtod(val[i], &end);
                if (end == val[i]) break;          /* a non-numeric binding is not a substitution */
                return ar_num(ar, v);
            }
        }
        /* pi and e are constants the grammar resolves everywhere (TOOL_SPEC 3.2); everything else
         * left standing is a variable the runtime could not bind, and the caller must be told --
         * silently comparing against a symbolic tree would make an unbindable record read clean. */
        if (strcmp(n->name, "pi") && strcmp(n->name, "e")) (*unbound)++;
        return ar_clone(ar, n);
    }
    node_t *c = ar_new(ar, n->t);
    if (!c) return 0;
    c->num = n->num;
    memcpy(c->name, n->name, sizeof c->name);
    c->nkid = n->nkid;
    for (int i = 0; i < n->nkid; i++) {
        c->kid[i] = subst(ar, n->kid[i], var, val, nvals, unbound);
        if (!c->kid[i]) return 0;
    }
    return c;
}

/* ---- reason reporting -------------------------------------------------------------------------
 * Cheapest true reason first: operators, then operands, then structure. Anything else would report
 * "different structure" for a dropped variable, which is true and useless. */
static void ops_of(const node_t *n, int *c) {
    if (!n) return;
    if (n->t != N_NUM && n->t != N_SYM) c[n->t]++;
    for (int i = 0; i < n->nkid; i++) ops_of(n->kid[i], c);
}

#define LEAF_MAX 32
/* A leaf is a number OR a symbol. An earlier version pushed NAN for symbols, which looks harmless
 * and is not: num_eq(NAN, NAN) is false, so every symbolic comparison -- the whole `solve` path --
 * reported "needs nan" for operands that matched perfectly. Carry the name. */
typedef struct { int is_num; double num; const char *name; } leaf_t;

static void leaves_of(const node_t *n, leaf_t *out, int *cnt) {
    if (!n || *cnt >= LEAF_MAX) return;
    if (n->t == N_NUM) { out[*cnt].is_num = 1; out[*cnt].num = n->num; out[(*cnt)++].name = 0; return; }
    if (n->t == N_SYM) { out[*cnt].is_num = 0; out[*cnt].num = 0;      out[(*cnt)++].name = n->name; return; }
    for (int i = 0; i < n->nkid; i++) leaves_of(n->kid[i], out, cnt);
}

static int leaf_eq(const leaf_t *a, const leaf_t *b) {
    if (a->is_num != b->is_num) return 0;
    return a->is_num ? num_eq(a->num, b->num) : !strcmp(a->name, b->name);
}

/* Render a leaf for the reason string. Two buffers so a caller can name both sides in one line. */
static const char *leaf_str(const leaf_t *l, char *buf, int cap) {
    if (!l->is_num) return l->name;
    snprintf(buf, (size_t)cap, "%.10g", l->num);
    return buf;
}

static const char *OPNAME[] = { "number", "symbol", "+", "-", "*", "/", "^", "unary-", "call", "=" };

#define MAXB 48
#define NAMEB 32
#define VALB 40

static char g_var[MAXB][NAMEB], g_val[MAXB][VALB];
static const char *g_varp[MAXB], *g_valp[MAXB];

/* Every `ident = number` in [doc, end). Later bindings do not overwrite earlier ones: the question
 * comes first in the document and is the student's own statement of the premise. */
static int scan_bindings(const char *doc, const char *end) {
    int n = 0;
    for (const char *p = doc; p < end && n < MAXB; p++) {
        if (!isalpha((unsigned char)*p) && *p != '_') continue;
        if (p > doc && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) continue;
        const char *s = p;
        while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
        int len = (int)(p - s);
        if (len <= 0 || len >= NAMEB) continue;
        const char *q = p;
        while (q < end && (*q == ' ' || *q == '\t')) q++;
        if (q >= end || *q != '=') { p--; continue; }
        q++;
        while (q < end && (*q == ' ' || *q == '\t')) q++;
        const char *vs = q;
        if (q < end && (*q == '-' || *q == '+')) q++;
        int digits = 0;
        while (q < end && (isdigit((unsigned char)*q) || *q == '.')) { if (isdigit((unsigned char)*q)) digits++; q++; }
        if (q < end && (*q == 'e' || *q == 'E')) {
            const char *save = q; q++;
            if (q < end && (*q == '-' || *q == '+')) q++;
            if (q < end && isdigit((unsigned char)*q)) { while (q < end && isdigit((unsigned char)*q)) q++; }
            else q = save;
        }
        int vlen = (int)(q - vs);
        if (!digits || vlen <= 0 || vlen >= VALB) { p--; continue; }
        int dup = 0;
        for (int i = 0; i < n; i++) if (!strncmp(g_var[i], s, (size_t)len) && g_var[i][len] == 0) dup = 1;
        if (!dup) {
            memcpy(g_var[n], s, (size_t)len); g_var[n][len] = 0;
            memcpy(g_val[n], vs, (size_t)vlen); g_val[n][vlen] = 0;
            n++;
        }
        p = q - 1;
    }
    return n;
}


/* ---- the document-level entry point ----------------------------------------------------------
 * THREE callers -- the device generation loop, tools/eval/shapecli for grade.py, and
 * train/select_run.py through the same CLI -- and one implementation. An earlier draft had the
 * document parsing in the CLI only, which would have left the device path either unchecked or
 * carrying a second copy of the binding rule. docs/WIRING_AUDIT.md: with two graders every check
 * has to be added twice or it silently covers half the surface. This makes it once.
 *
 * Bindings are read by the SAME rule provenance sources numbers by -- a `name = number` clause
 * before <a>. If the two disagreed about what counts as supplied, a call could be provenance-clean
 * and shape-unchecked for reasons neither check reported. */
int tlm_shape_check_doc(const char *doc, char *why, int cap) {
    if (why && cap > 0) why[0] = 0;
    if (!doc || !why || cap < 32) return TLM_SHAPE_UNCHECKED;

    const char *r = strstr(doc, "<r>");
    if (!r) { snprintf(why, (size_t)cap, "no <r> span in the document"); return TLM_SHAPE_UNCHECKED; }
    r += 3;
    char formula[160]; int fo = 0;
    for (const char *p = r; *p && *p != '|' && *p != '\n' && fo < (int)sizeof formula - 1; p++)
        if (*p != ' ') formula[fo++] = *p;
    formula[fo] = 0;
    if (!formula[0] || !strchr(formula, '=')) {
        /* A refusal document carries `none` here. Not a clean call -- a document the check does
         * not apply to, and it says which. */
        snprintf(why, (size_t)cap, "record has no relation (\"%s\")", formula);
        return TLM_SHAPE_UNCHECKED;
    }

    const char *aopen = strstr(doc, "<a>");
    int nb = scan_bindings(doc, aopen ? aopen : doc + strlen(doc));
    for (int i = 0; i < nb; i++) { g_varp[i] = g_var[i]; g_valp[i] = g_val[i]; }

    /* EVERY tool span, not just the last. A retry emits more than one, and a document whose first
     * call was wrong is not clean because its second was right. Worst status wins. */
    int worst = TLM_SHAPE_OK; char w2[256] = {0}; int calls = 0;
    for (const char *p = doc; (p = strstr(p, "<tool>")); ) {
        const char *e = strstr(p, "</tool>");
        if (!e) break;
        calls++;
        char span[MAX_CALL_BYTES];
        int len = (int)(e - p) + 7;
        if (len >= (int)sizeof span) len = (int)sizeof span - 1;
        memcpy(span, p, (size_t)len); span[len] = 0;
        int st = tlm_shape_check(formula, g_varp, g_valp, nb, span, w2, sizeof w2);
        if (st > worst) { worst = st; snprintf(why, (size_t)cap, "%s", w2); }
        if (st == TLM_SHAPE_MISMATCH) break;
        p = e + 7;
    }
    if (!calls) { snprintf(why, (size_t)cap, "no tool call in the document"); return TLM_SHAPE_UNCHECKED; }
    if (worst == TLM_SHAPE_OK) why[0] = 0;
    return worst;
}

/* ---- the check --------------------------------------------------------------------------------
 * One static arena. This runs once per emitted call on a single-threaded device, immediately after
 * generation halts and before the evaluator is invoked, so there is nothing to re-enter. Static
 * rather than stack because arena_t is MAX_NODES * sizeof(node_t) -- far too large for an Ndless
 * stack frame, and a stack overflow here is a hard reset with no fault handler. */
static arena_t G_AR;

int tlm_shape_check(const char *formula,
                    const char *const *var, const char *const *val, int nvals,
                    const char *span, char *why, int cap) {
    if (why && cap > 0) why[0] = 0;
    if (!formula || !span || !why || cap < 32)
        { if (why && cap > 0) snprintf(why, (size_t)cap, "bad arguments"); return TLM_SHAPE_UNCHECKED; }

    /* --- the function name, and whether this check applies to it --- */
    const char *p = strstr(span, "<tool>");
    const char *a1 = p ? strstr(p, "<arg>") : 0;
    const char *end = p ? strstr(p, "</tool>") : 0;
    if (!p || !a1 || !end || a1 > end)
        { snprintf(why, (size_t)cap, "no complete call span"); return TLM_SHAPE_UNCHECKED; }
    char fn[MAX_IDENT]; int fo = 0;
    for (const char *q = p + 6; q < a1 && fo < (int)sizeof fn - 1; q++) if (*q != ' ') fn[fo++] = *q;
    fn[fo] = 0;

    /* Argument 1 only, and only for the two functions whose first argument is the substituted
     * expression. `solve`, `diff`, `integ`, `evalat` and `stat` take a SYMBOLIC first argument, so
     * comparing it against a value-substituted tree would reject every correct call. They need
     * their own rule; until it exists they are UNCHECKED, which is not a pass. */
    int is_expr_call = (!strcmp(fn, "eval") || !strcmp(fn, "conv"));
    int is_solve     = (!strcmp(fn, "solve"));
    if (!is_expr_call && !is_solve) {
        snprintf(why, (size_t)cap, "no shape rule for '%s' -- NOT CHECKED, not clean", fn);
        return TLM_SHAPE_UNCHECKED;
    }

    const char *a1s = a1 + 5;
    const char *a1e = strstr(a1s, "<arg>");
    if (!a1e || a1e > end) a1e = end;
    int alen = (int)(a1e - a1s);
    if (alen <= 0 || alen >= MAX_ARG_BYTES)
        { snprintf(why, (size_t)cap, "argument 1 is empty or over %d bytes", MAX_ARG_BYTES);
          return TLM_SHAPE_UNCHECKED; }
    char arg[MAX_ARG_BYTES]; memcpy(arg, a1s, (size_t)alen); arg[alen] = 0;

    /* --- the expression the call must be --- */
    const char *rhs = strchr(formula, '=');
    if (!rhs) { snprintf(why, (size_t)cap, "record formula has no '='"); return TLM_SHAPE_UNCHECKED; }

    ar_reset(&G_AR);
    node_t *want = 0, *got = 0;

    if (is_solve) {
        /* `solve` argument 1 is the relation itself, unsubstituted. Compare it to the record. */
        if (parse_expr(&G_AR, formula, 1, &want) != E_NONE)
            { snprintf(why, (size_t)cap, "record formula does not parse"); return TLM_SHAPE_UNCHECKED; }
        if (parse_expr(&G_AR, arg, 1, &got) != E_NONE)
            { snprintf(why, (size_t)cap, "solve argument 1 does not parse as an equation");
              return TLM_SHAPE_MISMATCH; }
    } else {
        node_t *raw = 0;
        if (parse_expr(&G_AR, rhs + 1, 0, &raw) != E_NONE)
            { snprintf(why, (size_t)cap, "record right-hand side does not parse");
              return TLM_SHAPE_UNCHECKED; }
        int unbound = 0;
        want = subst(&G_AR, raw, var, val, nvals, &unbound);
        if (!want) { snprintf(why, (size_t)cap, "expression too large to check");
                     return TLM_SHAPE_UNCHECKED; }
        if (unbound) {
            /* THE mgh CASE, and it must not read as a pass. If the runtime could not bind every
             * variable, it cannot say what the call should be -- and a document built from this
             * prompt is unanswerable, which is a finding about the PROMPT, not the model. */
            snprintf(why, (size_t)cap,
                     "%d variable(s) in %s had no supplied value -- the prompt cannot support a "
                     "correct call, so the call cannot be checked", unbound, formula);
            return TLM_SHAPE_UNCHECKED;
        }
        if (parse_expr(&G_AR, arg, 0, &got) != E_NONE)
            { snprintf(why, (size_t)cap, "emitted argument does not parse as an expression");
              return TLM_SHAPE_MISMATCH; }
    }

    node_t *cw = sc_canon(&G_AR, want), *cg = sc_canon(&G_AR, got);
    if (!cw || !cg) { snprintf(why, (size_t)cap, "expression too large to canonicalise");
                      return TLM_SHAPE_UNCHECKED; }
    if (nodecmp(cw, cg) == 0) return TLM_SHAPE_OK;

    /* --- the most SPECIFIC true reason, which is not the same as the cheapest ---
     *
     * OPERANDS BEFORE OPERATORS, corrected from the first draft. Dropping a factor from a product
     * changes both multisets, and "U=m*g*h expects 2 '*', the call has 1" is true and nearly
     * useless next to "needs 9.81 and the call does not use it". An operator-count difference is
     * usually a CONSEQUENCE of a missing operand, so reporting it first buries the cause. Operators
     * are reported when the operands all match, which is exactly the case they describe well:
     * `p_0 + rho*g*h` emitted with a minus. */
    char b1[32];
    leaf_t lw[LEAF_MAX], lg[LEAF_MAX]; int nw = 0, ng = 0;
    leaves_of(cw, lw, &nw); leaves_of(cg, lg, &ng);
    int used[LEAF_MAX] = {0};
    for (int i = 0; i < nw; i++) {
        int hit = 0;
        for (int j = 0; j < ng && !hit; j++)
            if (!used[j] && leaf_eq(&lw[i], &lg[j])) { used[j] = 1; hit = 1; }
        if (!hit) {
            snprintf(why, (size_t)cap, "operand mismatch: %s needs %s and the call does not use it",
                     formula, leaf_str(&lw[i], b1, sizeof b1));
            return TLM_SHAPE_MISMATCH;
        }
    }
    for (int j = 0; j < ng; j++) if (!used[j]) {
        snprintf(why, (size_t)cap, "operand mismatch: the call uses %s, which %s does not",
                 leaf_str(&lg[j], b1, sizeof b1), formula);
        return TLM_SHAPE_MISMATCH;
    }
    int ow[N_EQ + 1] = {0}, og[N_EQ + 1] = {0};
    ops_of(cw, ow); ops_of(cg, og);
    for (int t = N_ADD; t <= N_EQ; t++) {
        if (ow[t] != og[t]) {
            snprintf(why, (size_t)cap, "operator mismatch: %s expects %d '%s', the call has %d",
                     formula, ow[t], OPNAME[t], og[t]);
            return TLM_SHAPE_MISMATCH;
        }
    }
    snprintf(why, (size_t)cap,
             "same operators and operands, different structure -- %s was rearranged", formula);
    return TLM_SHAPE_MISMATCH;
}
