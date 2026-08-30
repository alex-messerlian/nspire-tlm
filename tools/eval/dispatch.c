/* dispatch.c -- the tool interface from TOOL_SPEC.md section 7.
 *
 * One entry point, one arena, every path returns a status. No assert, no abort, no allocation.
 * Ordinary math errors come back as TB_OK with an error code in `out` -- the model is supposed to
 * see those and retry. TB_ERR is reserved for "the caller passed something structurally impossible",
 * which the runtime handles differently.
 */
#include "eval.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static arena_t g_arena;

typedef struct { const char *name; int arity; } fn_t;
static const fn_t FNS[] = {
    {"eval",1},{"evalat",3},{"solve",2},{"diff",2},{"integ",4},{"conv",2},{"stat",2}
};
#define NFNS ((int)(sizeof FNS / sizeof FNS[0]))

static tb_status fail(err_t e, char *out, size_t sz) {
    const char *c = err_code(e ? e : E_RANGE);
    if (strlen(c) + 1 > sz) { if (sz) out[0] = 0; return TB_ERR; }
    strcpy(out, c);
    return TB_OK;
}

/* Parse a scalar argument (a bound value or an integration limit). Units are not allowed here. */
static err_t scalar_arg(const char *s, double *v) {
    node_t *n;
    err_t e = parse_expr(&g_arena, s, 0, &n);
    if (e) return e;
    quant_t q;
    binds_t nb = {{0},{0},0};
    e = num_eval(n, &nb, 0, &q);
    if (e) return e;
    if (!dim_is_scalar(q.d)) return E_UNITS;
    *v = q.v;
    return E_NONE;
}

/* Match "<scalar expression> <affine-unit-name>", e.g. "25 degC". Returns 1 and fills `q` with the
 * SI (kelvin) value, plus the unit's offset, on success. */
static void strip_parens_inplace(char *s) {
    for (;;) {
        size_t L = strlen(s), b = 0;
        while (s[b] == ' ') b++;
        while (L > b && s[L-1] == ' ') L--;
        if (L - b >= 2 && s[b] == '(' && s[L-1] == ')') {
            int d = 0; size_t i;
            for (i = b; i < L; i++) { if (s[i]=='(') d++; else if (s[i]==')') { d--; if (!d) break; } }
            if (i != L - 1) { memmove(s, s + b, L - b); s[L-b] = 0; return; }
            memmove(s, s + b + 1, L - b - 2); s[L-b-2] = 0; continue;
        }
        memmove(s, s + b, L - b); s[L-b] = 0; return;
    }
}

static int affine_operand(const char *s, quant_t *q, double *offset) {
    size_t L = strlen(s);
    if (L == 0 || L >= MAX_ARG_BYTES) return 0;
    size_t e = L;
    while (e > 0 && s[e-1] == ' ') e--;
    size_t b = e;
    while (b > 0 && ((s[b-1] >= 'a' && s[b-1] <= 'z') || (s[b-1] >= 'A' && s[b-1] <= 'Z'))) b--;
    if (b == e || e - b >= MAX_IDENT) return 0;

    char unit[MAX_IDENT];
    memcpy(unit, s + b, e - b);
    unit[e - b] = 0;

    quant_t u; double off = 0;
    if (!units_lookup(unit, &u, &off) || off == 0.0) return 0;

    char pre[MAX_ARG_BYTES];
    memcpy(pre, s, b);
    pre[b] = 0;
    double v;
    if (scalar_arg(pre, &v) != E_NONE) return 0;

    q->v = v * u.v + off;      /* -> SI base (kelvin) */
    q->d = u.d;
    *offset = off;
    return 1;
}

/* A DIFFERENCE of two absolute temperatures is a legitimate interval, even though each operand
 * alone is affine and neither may enter a compound expression. Q=m*c*dT is unreachable without it:
 * "heat 2 kg of water from 20 degC to 50 degC" has no expressible dT. Recognised shape is exactly
 * "<scalar> <affine> - <scalar> <affine>" with the SAME unit on both sides; the result is an
 * interval in K, so the offsets cancel and only the scale applies. A SUM of two absolute
 * temperatures stays refused -- it has no physical meaning. */
static int affine_difference(const char *s, quant_t *q) {
    const char *m = NULL;
    int depth = 0;
    for (const char *c = s; *c; c++) {
        if (*c == '(') depth++;
        else if (*c == ')') depth--;
        else if (*c == '-' && depth == 0 && c != s) {
            const char *pv = c - 1;
            while (pv > s && *pv == ' ') pv--;
            if (*pv == '(' || *pv == '*' || *pv == '/' || *pv == '^' || *pv == '-' ||
                *pv == '+' || *pv == 'e' || *pv == 'E') continue;   /* sign, not subtraction */
            m = c;                                                   /* last top-level '-' wins */
        }
    }
    if (!m) return 0;

    char lhs[MAX_ARG_BYTES], rhs[MAX_ARG_BYTES];
    size_t ln = (size_t)(m - s);
    if (ln >= sizeof lhs || strlen(m + 1) >= sizeof rhs) return 0;
    memcpy(lhs, s, ln); lhs[ln] = 0;
    snprintf(rhs, sizeof rhs, "%s", m + 1);
    strip_parens_inplace(lhs); strip_parens_inplace(rhs);

    quant_t a, b; double oa = 0, ob = 0;
    if (!affine_operand(lhs, &a, &oa)) return 0;
    if (!affine_operand(rhs, &b, &ob)) return 0;
    if (oa != ob) return 0;                       /* mixing degC and degF: refuse, do not guess */

    q->v = a.v - b.v;                             /* both already converted to K by affine_operand */
    q->d = a.d; q->ang = 0;
    return 1;
}


static int valid_ident(const char *s) {
    if (!s || !*s || strlen(s) >= MAX_IDENT) return 0;
    for (const char *p = s; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_' ||
              (p != s && *p >= '0' && *p <= '9'))) return 0;
    return 1;
}

tb_status tool_dispatch(const char *name, const char *const *args, int nargs,
                        char *out, size_t out_sz) {
    if (!name || !args || !out || out_sz < 16) return TB_ERR;
    out[0] = 0;
    ar_reset(&g_arena);

    int arity = -1;
    for (int i = 0; i < NFNS; i++) if (!strcmp(FNS[i].name, name)) { arity = FNS[i].arity; break; }
    if (arity < 0)     return fail(E_NAME,  out, out_sz);
    if (nargs != arity) return fail(E_ARITY, out, out_sz);
    for (int i = 0; i < nargs; i++) {
        if (!args[i] || strlen(args[i]) >= MAX_ARG_BYTES) return fail(E_PARSE, out, out_sz);
    }

    err_t e;
    node_t *n = NULL;

    if (!strcmp(name, "eval")) {
        if ((e = parse_expr(&g_arena, args[0], 0, &n))) return fail(e, out, out_sz);
        quant_t q;
        binds_t nb = {{0},{0},0};
        /* use_units = 0: TOOL_SPEC v1.2.0 §3.2 -- units resolve ONLY in conv. */
        if ((e = num_eval(n, &nb, 0, &q))) return fail(e, out, out_sz);
        if ((e = fmt_quant(q, out, out_sz))) return fail(e, out, out_sz);
        return TB_OK;
    }

    if (!strcmp(name, "evalat")) {
        if (!valid_ident(args[1])) return fail(E_EXPR, out, out_sz);
        double at;
        if ((e = scalar_arg(args[2], &at))) return fail(e, out, out_sz);
        ar_reset(&g_arena);
        if ((e = parse_expr(&g_arena, args[0], 0, &n))) return fail(e, out, out_sz);
        binds_t b = {{0},{0},0};
        b.name[0] = args[1]; b.val[0] = at; b.n = 1;
        quant_t q;
        /* use_units = 0: see above. m, g, h, t, s are physics variables here, not units. */
        if ((e = num_eval(n, &b, 0, &q))) return fail(e, out, out_sz);
        if ((e = fmt_quant(q, out, out_sz))) return fail(e, out, out_sz);
        return TB_OK;
    }

    if (!strcmp(name, "solve")) {
        if (!valid_ident(args[1])) return fail(E_EXPR, out, out_sz);
        if ((e = parse_expr(&g_arena, args[0], 1, &n))) return fail(e, out, out_sz);
        e = solve_eq(&g_arena, n, args[1], out, out_sz);
        if (e == E_NOSOL) {
            /* Numeric extraction failed. Try symbolic-coefficient isolation before giving up --
             * this is the whole formula-rearrangement class (F=ma for a, v=d/t for t). */
            e = literal_solve(&g_arena, n, args[1], out, out_sz);
        }
        if (e) return fail(e, out, out_sz);
        return TB_OK;
    }

    if (!strcmp(name, "diff")) {
        if (!valid_ident(args[1])) return fail(E_EXPR, out, out_sz);
        if ((e = parse_expr(&g_arena, args[0], 0, &n))) return fail(e, out, out_sz);
        node_t *dn;
        if ((e = deriv(&g_arena, n, args[1], &dn))) return fail(e, out, out_sz);
        if ((e = simplify(&g_arena, dn, &dn)))      return fail(e, out, out_sz);
        if ((e = canon(&g_arena, dn, args[1], &dn))) return fail(e, out, out_sz);
        if ((e = render(dn, args[1], out, out_sz))) return fail(e, out, out_sz);
        return TB_OK;
    }

    if (!strcmp(name, "integ")) {
        if (!valid_ident(args[1])) return fail(E_EXPR, out, out_sz);
        double lo, hi;
        if ((e = scalar_arg(args[2], &lo))) return fail(e, out, out_sz);
        if ((e = scalar_arg(args[3], &hi))) return fail(e, out, out_sz);
        ar_reset(&g_arena);
        if ((e = parse_expr(&g_arena, args[0], 0, &n))) return fail(e, out, out_sz);
        double v;
        if ((e = integrate(n, args[1], lo, hi, &v))) return fail(e, out, out_sz);
        if ((e = fmt_number(v, out, out_sz)))        return fail(e, out, out_sz);
        return TB_OK;
    }

    if (!strcmp(name, "conv")) {
        quant_t from;
        binds_t nb = {{0},{0},0};

        /* Affine units (degC, degF) carry an offset, so they are meaningless inside a compound
         * expression and num_eval refuses them. They ARE meaningful as a whole conv argument, which
         * is the only place a temperature conversion can be well defined. Handle that shape here:
         * "<scalar> <affine-unit>". */
        double from_off = 0;
        if (!affine_operand(args[0], &from, &from_off) && !affine_difference(args[0], &from)) {
            if ((e = parse_expr(&g_arena, args[0], 0, &n))) return fail(e, out, out_sz);
            if ((e = num_eval(n, &nb, 1, &from))) return fail(e, out, out_sz);
        }

        quant_t to; double off = 0;
        if (!units_lookup(args[1], &to, &off)) {
            /* Not a bare unit name -- allow a compound target like "m/s". */
            ar_reset(&g_arena);
            node_t *tn;
            if ((e = parse_expr(&g_arena, args[1], 0, &tn))) return fail(E_UNITS, out, out_sz);
            binds_t nb2 = {{0},{0},0};
            if ((e = num_eval(tn, &nb2, 1, &to))) return fail(E_UNITS, out, out_sz);
            off = 0;
        }
        if (!dim_eq(from.d, to.d)) return fail(E_UNITS, out, out_sz);
        if (to.v == 0)             return fail(E_UNITS, out, out_sz);

        /* TOOL_SPEC section 8 invariant 6: no angle constant inside a rate. A surviving angle
         * factor makes Hz wrong by exactly 2*pi -- (5 rev)/(2 s) is 15.708 rad/s and 2.5 Hz, and
         * the dimension vector 1/s cannot tell those apart. Refuse rather than name it. A syntactic
         * scan of the argument text would be wrong here: sin(30 deg)/(2 s) IS a legitimate 0.25 Hz,
         * because sin consumed the angle. Only the taint bit distinguishes them. */
        if (from.ang && !strcmp(args[1], "Hz")) return fail(E_UNITS, out, out_sz);

        double v = off != 0.0 ? (from.v - off) / to.v : from.v / to.v;
        char num[64];
        if ((e = fmt_number(v, num, sizeof num))) return fail(e, out, out_sz);

        /* F3, signed off 2026-08-27: a target of literal "1" renders BARE. `conv((50 J)/(200 J), 1)`
         * gave "0.25 1", and that text goes verbatim into training data as the model's answer.
         * "0.25" is what a physicist writes.
         *
         * NARROW ON PURPOSE -- the test is the literal target string, not dimensionlessness. `%`,
         * `rad` and `sr` are all dimensionless and all must survive: DIMENSIONLESS_AUDIT C6 requires
         * counts to keep scale 1 without folding to an angle, and C7 requires `sr` never to vanish
         * silently. A rule keyed on `dim_eq(to.d, DIM_NONE)` would delete exactly those, which is
         * the broader-and-wrong version of this fix. */
        if (!strcmp(args[1], "1")) {
            if (strlen(num) + 1 > out_sz) return fail(E_RANGE, out, out_sz);
            strcpy(out, num);
            return TB_OK;
        }
        if (strlen(num) + 1 + strlen(args[1]) + 1 > out_sz) return fail(E_RANGE, out, out_sz);
        snprintf(out, out_sz, "%s %s", num, args[1]);
        return TB_OK;
    }

    if (!strcmp(name, "stat")) {
        double v;
        if ((e = stat_op(args[0], args[1], &v))) return fail(e, out, out_sz);
        if ((e = fmt_number(v, out, out_sz)))    return fail(e, out, out_sz);
        return TB_OK;
    }

    return fail(E_NAME, out, out_sz);
}

/* ---- ASCII wire-format parsing -------------------------------------------------------------- */
/* The special-token form is the runtime's job; by the time text reaches here the delimiters are
 * literal. TOOL_SPEC.md section 2.1. */

tb_status tool_call_text(const char *call, char *out, size_t out_sz) {
    if (!call || !out || out_sz < 16) return TB_ERR;
    out[0] = 0;
    if (strlen(call) > MAX_CALL_BYTES) return fail(E_PARSE, out, out_sz);

    static char buf[MAX_CALL_BYTES + 1];
    const char *p = call;
    if (strncmp(p, "<tool>", 6)) return fail(E_PARSE, out, out_sz);
    p += 6;

    const char *end = strstr(p, "</tool>");
    if (!end) return fail(E_PARSE, out, out_sz);
    if (end[7] != 0) return fail(E_PARSE, out, out_sz);

    size_t body = (size_t)(end - p);
    if (body >= sizeof buf) return fail(E_PARSE, out, out_sz);
    memcpy(buf, p, body);
    buf[body] = 0;

    static char name[MAX_IDENT];
    static char argbuf[MAX_ARGS][MAX_ARG_BYTES];
    const char *args[MAX_ARGS];
    int nargs = 0;

    char *cur = buf;
    char *sep = strstr(cur, "<arg>");
    size_t nlen = sep ? (size_t)(sep - cur) : strlen(cur);
    if (nlen == 0 || nlen >= MAX_IDENT) return fail(E_PARSE, out, out_sz);
    memcpy(name, cur, nlen); name[nlen] = 0;
    /* TRIM THE NAME, exactly as the arguments below are trimmed.
     *
     * THE ARGUMENTS WERE TRIMMED AND THE NAME WAS NOT, and that one-sided normalisation is the
     * whole of the device's "!name". TOOL_SPEC 2.2 says "no whitespace adjacent to any delimiter",
     * and the CORPUS honours it -- 16,459 of 16,459 sampled calls are `<tool>eval` with no space.
     * But the model does not emit corpus bytes, it emits TOKENS, and the tokenizer's decode
     * reintroduces the leading space: `<tool> eval<arg> 0.5*(2.0)*((3.0))^(2)</tool>`. The name
     * became " eval", arity lookup failed, and the runtime returned !name.
     *
     * The model is then handed <res> !name</res> and invents a number -- measured on device:
     *   <tool> eval<arg> 0.5*(2.0)*((3.0))^(2)</tool><res> !name</res>
     *   <a> The kinetic energy is -2.4 J. From K=0.5*m*(v)^(2).<end>
     * The call is CORRECT and the arithmetic is the runtime's; "-2.4 J" is the best continuation
     * of a poisoned result. One defect, reported as two.
     *
     * IT WAS INVISIBLE ON THE HOST because seven harnesses -- score_arms, e2e, capability,
     * format_sweep, l2, verbatim_retest, attempt_policy -- each call `call.replace(" ", "")`
     * before evalcli. Every one of them normalises what the runtime does not, so the graders were
     * strictly more forgiving than the device and no host measurement could see it. The fix goes
     * HERE, in the one path both sides share, and the harnesses stop stripping. */
    {   char *s = name;
        while (*s == ' ') s++;
        size_t L = strlen(s);
        while (L && s[L - 1] == ' ') s[--L] = 0;
        if (s != name) memmove(name, s, L + 1);
        if (!name[0]) return fail(E_PARSE, out, out_sz);
    }

    while (sep) {
        cur = sep + 5;
        sep = strstr(cur, "<arg>");
        size_t alen = sep ? (size_t)(sep - cur) : strlen(cur);
        if (nargs >= MAX_ARGS)      return fail(E_ARITY, out, out_sz);
        if (alen >= MAX_ARG_BYTES)  return fail(E_PARSE, out, out_sz);
        memcpy(argbuf[nargs], cur, alen);
        argbuf[nargs][alen] = 0;
        /* Trim, per TOOL_SPEC.md section 2.2. */
        char *s = argbuf[nargs];
        while (*s == ' ') s++;
        size_t L = strlen(s);
        while (L && s[L - 1] == ' ') s[--L] = 0;
        args[nargs] = s;
        nargs++;
    }

    return tool_dispatch(name, args, nargs, out, out_sz);
}
