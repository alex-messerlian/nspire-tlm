# Tool call format specification

**Status: FROZEN v1.1.0.** Data generation, the evaluator, the training pipeline, and the device
runtime all depend on this document. Changing it invalidates every generated corpus and every trained
checkpoint.

Change procedure: bump the version, write a migration note, regenerate all data, retrain. There is no
cheap edit. That is deliberate.

### Changelog

**v1.1.0** — amended before any data generation, so no migration was required.
- **§5.4 added: ambiguous dimension vectors are never collapsed to a derived unit name.** v1.0.0
  rendered every coherent SI dimension as its derived symbol, which made torque print as joules.
  A dimension vector cannot distinguish energy from torque, and emitting a guess would compile
  "torque is measured in joules" into the weights — the first error a physics judge catches.
- **§5.3 sum-term ordering is now implemented**, not just specified. v1.0.0 described
  descending-degree ordering that the evaluator did not perform. Spec and code now agree.
- §8.5 added: the data generator must name target units explicitly via `conv` for blocked dimensions.

---

## 1. The one rule that matters most

**The model never emits a result. The model never computes a value.**

During training, result spans are present in the sequence but **masked out of the loss**. The model
learns to *condition on* results, not to *produce* them.

If results are left in the loss, the model learns to predict plausible-looking numbers — which is
exactly the failure this entire architecture exists to prevent. A 45M model that has learned to
hallucinate `<res>` content is strictly worse than one with no tools at all, because it is wrong with
formatting that looks authoritative.

Loss mask, per token:

| Span | In loss? |
|---|---|
| Prose | yes |
| `<tool>`, function name, `<arg>`, arguments, `</tool>` | yes |
| `<res>` … `</res>` including delimiters | **no** |

## 2. Wire format

```
<tool>NAME<arg>ARG₁<arg>ARG₂…<arg>ARGₙ</tool>
```

The runtime detects `</tool>`, halts generation, executes, and appends:

```
<res>RESULT</res>
```

Generation then resumes with the result in context.

Worked example — the full sequence as it appears in a training document:

```
To find where the parabola crosses the x-axis, set y to zero and solve.
<tool>solve<arg>2x^2+3x-5=0<arg>x</tool><res>x=-2.5, x=1</res>
The roots are x = -2.5 and x = 1, so the parabola crosses at those two points.
```

### 2.1 Delimiters are special tokens

`<tool>`, `<arg>`, `</tool>`, `<res>`, `</res>` are **five reserved single tokens** added to the
tokenizer. Not literal text.

This is not cosmetic:
- One token per delimiter instead of 3–5 BPE pieces. At 45M, emitting a delimiter reliably has to be
  a single high-probability decision, not a five-step sequence with five chances to derail.
- Special tokens cannot occur in natural text, so **there is no escaping problem** and no way for
  document content to forge a call or a result.
- Detection in the decode loop is an integer compare against a token id, not string matching on a
  partial UTF-8 stream.

**Fallback if an off-the-shelf tokenizer is used:** the literal ASCII strings above, with the
runtime scanning decoded text. Accept the reliability cost and say so in the writeup. Rejected as
the default: JSON. Quotes, braces, colons, commas and escaping are all token-expensive and all offer
the model ways to emit something unparseable.

### 2.2 Lexical rules

- No whitespace adjacent to any delimiter token.
- Arguments are trimmed of leading/trailing spaces before dispatch.
- Function names are lowercase ASCII, from the closed set in §3. Nothing else is valid.
- Argument bytes: printable ASCII only. A byte outside `0x20`–`0x7E` yields `!parse`.

### 2.3 Hard limits

Enforced by the runtime, not by the model's good behaviour. These exist because the device has no
preemption and no way to escape a runaway loop — a hung generation means the calculator is dead until
a battery pull, in front of a judge.

| Limit | Value | On violation |
|---|---|---|
| Max bytes per argument | 128 | `!parse` |
| Max bytes per call, `<tool>` to `</tool>` | 512 | `!parse` |
| Max arguments | 4 | `!arity` |
| Max tool calls per response | 8 | no further calls; strip and continue as prose |
| Max attempts per call site | 2 | degrade, §6.2 |
| Max integrand evaluations | 4096 | `!range` |
| Max simplifier rewrite passes | 32 | return best-so-far |

## 3. Function set — closed, fixed arity

Seven functions. Closed set and fixed arity are what make validation trivial and let the model
memorise the whole interface at 45M.

| Name | Arity | Arguments | Returns |
|---|---|---|---|
| `eval` | 1 | expression | number, optionally with unit |
| `evalat` | 3 | expression, variable, value | number |
| `solve` | 2 | equation, variable | root list |
| `diff` | 2 | expression, variable | expression |
| `integ` | 4 | expression, variable, lower, upper | number |
| `conv` | 2 | quantity, target unit | number with unit |
| `stat` | 2 | op, list | number |

`stat` ops, also closed: `mean` `median` `sd` `var` `sum` `min` `max` `n`.
`sd` and `var` are **sample** statistics (n−1 denominator). Stated because it is the single most
common silent wrongness in a stats tool.

### 3.1 Scope boundaries, deliberate

- **No symbolic integration.** `integ` is numeric and definite. Per the architecture decision.
- **`solve` handles degree ≤ 2 only.** Higher degree returns `!nosol`. Not "attempts and fails" —
  refuses cleanly, which is a behaviour the model can learn to route around.
- **No limits, series, matrices, or ODEs.** Out of scope for v1.

### 3.2 Identifier resolution — units vs. symbols

The one genuine ambiguity in the grammar. Is `m` a variable or metres?

**Rule: units are recognised only in `eval`, `evalat`, and `conv`. In `solve`, `diff`, and `integ`,
every bare identifier is a symbol.**

So `eval(9.8 m/s^2 * 3 s)` gives `29.4 m/s`, while `solve(m*x+b=0, x)` treats `m` and `b` as symbols.
Per-function, no context sensitivity, predictable from the function name alone — which means the
model can learn it.

Known limitation, accepted: you cannot do unit-carrying symbolic calculus. Attach units to `integ`
results by hand in the prose.

## 4. Expression grammar

Shared by every argument that holds an expression.

```
expr    := term (('+' | '-') term)*
term    := factor (('*' | '/') factor | implicit-mul factor)*
factor  := unary ('^' factor)?              // right associative
unary   := ('-' | '+')* postfix
postfix := primary ('!' )?                  // factorial, integers only
primary := NUMBER | IDENT | IDENT '(' expr (',' expr)* ')' | '(' expr ')' | '|' expr '|'
```

- **Implicit multiplication** is supported: `2x`, `3(x+1)`, `2 sin(x)`. Required — textbook notation
  uses it constantly and a model trained on textbooks will emit it.
- `^` is right-associative: `2^3^2` = 2^9 = 512.
- Unary minus binds looser than `^`: `-x^2` = `-(x^2)`.
- Functions: `sin cos tan asin acos atan sinh cosh tanh exp ln log log10 sqrt abs floor ceil round
  min max`. Trig in **radians**.
- Constants: `pi`, `e`. In unit-aware contexts `e` is the constant, never the unit "erg" or an
  exponent marker; write exponents as `1.5e3` which the lexer takes as part of the numeric literal.
- Equations, `solve` only: exactly one `=` at the top level.

## 5. Result format — canonical, backend independent

**Every backend must normalise into this grammar.** Backend 2 (TI math server) returns TI-formatted
UCS-2 with Private Use Area code points; its adapter is responsible for converting to this form. The
model must never be able to tell which backend answered.

### 5.1 Numbers

Determinism is a hard requirement: the same call must produce byte-identical output on host and
device, forever. Training data is generated by executing these calls; if the device formats
differently, every trained association is subtly wrong.

`printf("%g")` is **not** acceptable — its behaviour varies across libc implementations, and we have
glibc/Apple libc on the host and newlib on the device.

Algorithm, implemented once in `tools/eval/fmt.c` and shared:

1. Non-finite → `!domain` (NaN, ±inf from a defined operation) or `!range` (overflow).
2. Round to **10 significant decimal digits**.
3. If the rounded magnitude is 0, emit `0`.
4. If 1e-4 ≤ |x| < 1e10: fixed notation, trailing zeros stripped, trailing `.` stripped.
5. Otherwise: scientific, `d.dddddddddde±dd`, mantissa trailing zeros stripped.
6. Negative numbers get a leading `-`. Negative zero prints `0`.

### 5.2 Composite results

| Kind | Format | Example |
|---|---|---|
| Number with unit | `VALUE` SP `UNIT` | `29.4 m/s` |
| Root list | `var=VALUE` comma-space separated, **ascending by value** | `x=-2.5, x=1` |
| Complex roots | `var=a+bi` / `var=a-bi`, real part first | `x=1+2i, x=1-2i` |
| No roots | `none` | |
| Every value is a root | `all` | |
| Expression | canonical infix, **no spaces** | `6*x+3` |

Root ordering is specified because unordered output is nondeterministic output.

### 5.3 Canonical expression form

`diff` output must be canonicalised or the same derivative prints differently run to run.
**Implemented in `deriv.c:canon()`; verified by test.** Spec and code agree — do not let them drift.

- Explicit `*` between all factors. No implicit multiplication on output.
- No spaces.
- Terms ordered by descending degree in the differentiation variable; ties broken by ASCII on the
  rendered term.
- Constant folding applied. Identities `x*1→x`, `x+0→x`, `x^1→x`, `x^0→1`, `0*x→0` applied to fixpoint,
  capped at 32 passes.
- Parentheses only where precedence requires them. A leading unary minus does not parenthesise a
  product or quotient: `-2*x`, not `-(2*x)`. This is safe because the simplifier hoists negation out
  of `*` and `/`, so a `NEG` node never sits where dropping parens would change the parse.
- Ordering caveat: a function of the variable has no polynomial degree and is treated as degree 0,
  ordered against other degree-0 terms by rendered text. Deterministic, but it is not a claim about
  calculus.

### 5.4 Ambiguous dimensions are never named

Values carry SI dimension vectors, and output collapses a coherent dimension to its derived symbol —
`6 N`, `24 W`, `50 Pa`. **Two dimension vectors are excluded from that collapse and always render in
base units:**

| Dimension | Collides | Renders as |
|---|---|---|
| `m^2*kg/s^2` | energy (J) **vs** torque (N*m) | `m^2*kg/s^2` |
| `1/s` | frequency (Hz) **vs** angular velocity (rad/s) **vs** decay constant | `1/s` |

A dimension vector carries no information that separates these. Guessing writes a physics error
directly into the corpus, and therefore into the weights. Base units are always *correct*, merely
verbose — and §8.5 makes the verbosity the generator's problem, not the model's.

Deliberately **not** blocked: `Pa` is shared by pressure, stress and Young's modulus, but all three
are correctly written `Pa`, so the collapse is right in every case.

To extend the blocklist, remove the entry from `DERIVED[]` in `tools/eval/units.c` and add a row
above. The cost of blocking a vector unnecessarily is verbose output; the cost of failing to block an
ambiguous one is a wrong physics fact in the training data. Bias toward blocking.

## 6. Errors

### 6.1 Closed error set

Errors are part of the model's vocabulary — it must learn to recover from them — so the set is closed
and the strings are short.

| Code | Meaning |
|---|---|
| `!parse` | The call itself was malformed: bad delimiters, limits exceeded, non-printable bytes |
| `!name` | Function name not in §3 |
| `!arity` | Wrong argument count for that name |
| `!expr` | An argument failed to parse as an expression |
| `!domain` | Mathematically undefined: ÷0, `ln` of ≤0, `asin` outside [−1,1] |
| `!units` | Incompatible dimensions: adding metres to seconds, converting kg to volts |
| `!nosol` | Well-formed but this backend cannot solve it: degree > 2, non-polynomial |
| `!range` | Overflow, underflow to a value that is not representable, or an evaluation cap hit |
| `!give` | Runtime signal: attempts exhausted, answer without tools |

Emitted inside the normal wrapper: `<res>!domain</res>`. One code path, one conditioning pattern.

### 6.2 Retry and degrade

Per call site:

1. **Attempt 1** fails → runtime injects `<res>!code</res>`. The model gets one chance to emit a
   corrected call.
2. **Attempt 2** fails → runtime injects `<res>!give</res>`. The model must answer in prose without
   tool support, and the training data teaches it to hedge appropriately when it does.
3. There is no attempt 3.

The response-level cap of 8 calls is independent and absolute.

### 6.3 Never crash

Every entry point returns a status. No `assert`, no `abort`, no unchecked allocation, no unbounded
recursion in the parser or the simplifier. A tool failure degrades to `!give` and prose; it never
takes down the process. On the device there is nothing to catch a crash and nothing to restart it.

## 7. Backend interface

One interface, two backends, chosen at build time or runtime:

```c
typedef enum { TB_OK, TB_ERR } tb_status;

/* Fills `out` with a §5-canonical result, or a §6.1 error code including the '!'.
 * Returns TB_ERR only on failures the caller must handle differently from a math error;
 * ordinary math errors return TB_OK with an error string in `out`. */
tb_status tool_dispatch(const char *name, const char *const *args, int nargs,
                        char *out, size_t out_sz);
```

- **Backend 1** — `tools/eval/`, our own C. Mandatory. Runs on host **and** device. Host execution is
  a hard dependency of data generation, so this backend is on the critical path regardless of what
  Backend 2 turns out to be.
- **Backend 2** — TI math server via syscalls 339/342. Device only, research, upgrade not requirement.
  See `docs/BACKEND2_TI_MATH.md`. Its adapter owns UCS-2 ↔ ASCII transcoding, PUA substitution, and
  mapping TI's undocumented error numbers onto §6.1.

**Differential testing is the acceptance gate for Backend 2**, if it ever lands: run the entire
Backend 1 test corpus through Backend 2 and require byte-identical §5 output. Any divergence is a
Backend 2 bug, because Backend 1 defines the contract — it is what the model was trained against.

## 8. Data generation invariants

Enforced by the generator, not hoped for:

1. **Every tool call in generated data is executed at generation time.** A call that errors is either
   fixed or the sample is dropped. No unverified calls reach training.
2. **Every numeric claim in prose is backed by a preceding tool call in the same document.** This is
   what makes the "correctness with vs. without tools" metric meaningful rather than a comparison of
   two kinds of guessing.
3. **Result spans are byte-identical to what the evaluator emitted.** Never hand-written, never
   reformatted.
4. **Error-recovery samples are included on purpose**, at a target rate of roughly 5% of tool-using
   documents: a malformed call, a `!code`, a corrected call, a correct answer. The model cannot
   learn §6.2 recovery from data where every call succeeds.
5. **Any physics quantity whose dimension is on the §5.4 blocklist must be produced with `conv` and
   an explicit target unit**, never with bare `eval`. The generator knows from the problem context
   whether it is computing energy or torque; the evaluator does not and must not guess.

   ```
   wrong:  <tool>eval<arg>0.5*80 kg*(20 m/s)^2</tool>      -> 16000 m^2*kg/s^2
   right:  <tool>conv<arg>0.5*80 kg*(20 m/s)^2<arg>J</tool> -> 16000 J
   right:  <tool>conv<arg>50 N*m<arg>N*m</tool>             -> 50 N*m
   right:  <tool>conv<arg>1/(0.5 s)<arg>Hz</tool>           -> 2 Hz
   right:  <tool>conv<arg>2*pi*3/(1 s)<arg>rad/s</tool>     -> 18.84955592 rad/s
   ```

   This is a lint the generator can enforce mechanically: if an `eval` result ends in
   `m^2*kg/s^2` or `1/s`, the sample is rejected and regenerated as a `conv`.

## 9. Metrics this spec makes measurable

- **Tool call validity rate** — emitted calls that parse and execute, over all emitted calls. Split
  by attempt 1 vs. attempt 2 so recovery is visible separately from first-shot accuracy.
- **Answer correctness with tools vs. without** — same checkpoint, same questions, tool layer enabled
  and disabled. The difference is the architecture's entire justification, so it is the headline
  number.
- **Result-span leak rate** — how often the model tries to emit a `<res>` token. Should be ~0 given
  §1's loss mask. **A nonzero value means the mask is broken**, and it is a cheap continuous check on
  the most dangerous possible bug in the pipeline.
