/* eval.h -- Backend 1: our own math evaluator.
 *
 * Implements the contract in docs/TOOL_SPEC.md. This code runs on BOTH the host (where it verifies
 * every tool call during training-data generation) and the device (where it serves the model at
 * inference time). Those two must agree byte for byte, so:
 *
 *   - No printf("%g") anywhere. See fmt.c for why and what replaces it.
 *   - No malloc in the evaluation path. One fixed arena, bump allocated, reset per call.
 *   - No unbounded recursion. Every recursive descent carries a depth counter.
 *   - No assert, no abort, no exit. Every path returns a status. TOOL_SPEC.md section 6.3.
 *
 * `double` and libm are used freely. The device has no FPU, so this is soft-float and slow -- but a
 * tool call happens once per model call, not once per token, so it is nowhere near the hot loop.
 * Do not "optimise" this to fixed point; it would cost accuracy for no measurable throughput.
 */
#ifndef EVAL_H
#define EVAL_H

#include <stddef.h>

/* ---- Errors: the closed set from TOOL_SPEC.md section 6.1 ---------------------------------- */
typedef enum {
    E_NONE = 0, E_PARSE, E_NAME, E_ARITY, E_EXPR, E_DOMAIN, E_UNITS, E_NOSOL, E_RANGE
} err_t;

const char *err_code(err_t e);      /* -> "!domain" etc. E_NONE -> "" */

/* ---- Limits (TOOL_SPEC.md section 2.3) ------------------------------------------------------ */
#define MAX_ARG_BYTES   128
#define MAX_CALL_BYTES  512
#define MAX_ARGS        4
#define MAX_IDENT       24
#define MAX_NODES       1024
#define MAX_DEPTH       64
#define MAX_INTEG_EVALS 4096
#define MAX_SIMP_PASSES 32
#define MAX_RESULT      256
#define MAX_LIST        256

/* ---- Dimensions ----------------------------------------------------------------------------- */
/* Exponents over the SI base units, in this fixed order. */
enum { DIM_M, DIM_KG, DIM_S, DIM_A, DIM_K, DIM_MOL, DIM_CD, DIM_COUNT };

typedef struct { signed char e[DIM_COUNT]; } dim_t;

/* `ang` marks a value that carries a SURVIVING angle factor -- one contributed by deg/rad/rev and
 * not yet consumed by a trigonometric function. Angles are dimensionless, so `d` cannot see them,
 * which is how (5 rev)/(2 s) came to render as "15.70796327 Hz" when the frequency is 2.5 Hz.
 * See TOOL_SPEC section 8 invariant 6. Consumed by sin/cos/tan; produced by asin/acos/atan. */
typedef struct { double v; dim_t d; unsigned char ang; } quant_t;   /* v is always in SI base units */

int   dim_eq(dim_t a, dim_t b);
int   dim_is_scalar(dim_t d);
dim_t dim_add(dim_t a, dim_t b);                 /* multiply quantities  -> add exponents */
dim_t dim_sub(dim_t a, dim_t b);                 /* divide quantities    -> sub exponents */
dim_t dim_scale(dim_t a, int k);                 /* raise to power k */

/* Look up a unit name. Returns 1 on hit, filling `q` with the SI value of ONE of that unit
 * (e.g. "km" -> v=1000, d=[m]). `offset` is nonzero only for affine units (degC, degF), which are
 * legal only as a whole argument to conv, never inside a compound expression. */
int units_lookup(const char *name, quant_t *q, double *offset);

/* Render a dimension in canonical form, e.g. "m/s^2". Empty string for scalars. */
void units_render(dim_t d, char *out, size_t sz);

/* ---- Deterministic formatting (TOOL_SPEC.md section 5.1) ------------------------------------ */
/* Returns E_RANGE / E_DOMAIN instead of ever emitting "nan" or "inf". */
err_t fmt_number(double x, char *out, size_t sz);
err_t fmt_quant(quant_t q, char *out, size_t sz);

/* ---- AST ------------------------------------------------------------------------------------ */
typedef enum {
    N_NUM, N_SYM, N_ADD, N_SUB, N_MUL, N_DIV, N_POW, N_NEG, N_CALL, N_EQ
} ntype_t;

typedef struct node {
    ntype_t t;
    double  num;                 /* N_NUM */
    char    name[MAX_IDENT];     /* N_SYM, N_CALL */
    struct node *kid[MAX_ARGS];
    int     nkid;
} node_t;

/* Arena: bump allocator, reset per tool call. No free(), no leaks, no fragmentation. */
typedef struct { node_t pool[MAX_NODES]; int n; } arena_t;

void    ar_reset(arena_t *a);
node_t *ar_new(arena_t *a, ntype_t t);           /* NULL when exhausted -> caller returns E_RANGE */
node_t *ar_num(arena_t *a, double v);
node_t *ar_sym(arena_t *a, const char *name);
node_t *ar_bin(arena_t *a, ntype_t t, node_t *l, node_t *r);
node_t *ar_clone(arena_t *a, const node_t *n);

/* ---- Parser --------------------------------------------------------------------------------- */
/* `allow_eq` permits exactly one top-level '=' (solve only). */
err_t parse_expr(arena_t *a, const char *src, int allow_eq, node_t **out);

/* ---- Numeric evaluation --------------------------------------------------------------------- */
/* Variable bindings, used by evalat and by the integrator. */
typedef struct {
    const char *name[MAX_ARGS];
    double      val[MAX_ARGS];
    int         n;
} binds_t;

/* `use_units`: resolve bare identifiers against the unit table (eval/evalat/conv) or treat them as
 * unbound symbols and fail (solve/diff/integ). TOOL_SPEC.md section 3.2. */
err_t num_eval(const node_t *n, const binds_t *b, int use_units, quant_t *out);

/* ---- Symbolic differentiation --------------------------------------------------------------- */
err_t deriv(arena_t *a, const node_t *n, const char *var, node_t **out);
err_t simplify(arena_t *a, node_t *n, node_t **out);
err_t canon(arena_t *a, node_t *n, const char *var, node_t **out);     /* sum-term ordering */
err_t render(const node_t *n, const char *var, char *out, size_t sz);   /* canonical infix */

/* ---- Solve ---------------------------------------------------------------------------------- */
err_t solve_eq(arena_t *a, const node_t *eq, const char *var, char *out, size_t sz);

/* E1/E2/E3r: symbolic-coefficient solving, tried only after solve_eq returns E_NOSOL. See
 * literal.c and docs/P1_PROBE_PRELIM.md. */
err_t literal_solve(arena_t *a, const node_t *eq, const char *var, char *out, size_t sz);

/* ---- Numeric integration -------------------------------------------------------------------- */
err_t integrate(const node_t *f, const char *var, double lo, double hi, double *out);

/* ---- Statistics ----------------------------------------------------------------------------- */
err_t stat_op(const char *op, const char *list, double *out);

/* ---- Tool dispatch (TOOL_SPEC.md section 7) ------------------------------------------------- */
typedef enum { TB_OK, TB_ERR } tb_status;

tb_status tool_dispatch(const char *name, const char *const *args, int nargs,
                        char *out, size_t out_sz);

/* Parse a complete "<tool>name<arg>a<arg>b</tool>" span and dispatch it. Accepts the ASCII fallback
 * form; the special-token form is the runtime's job to convert into this. */
tb_status tool_call_text(const char *call, char *out, size_t out_sz);

#endif /* EVAL_H */
