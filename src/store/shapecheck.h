#ifndef NS_SHAPECHECK_H
#define NS_SHAPECHECK_H
/* Structural call validation -- docs/ARCHITECTURE.md section 6.
 *
 * THE GAP THIS CLOSES. The runtime holds the record's relation AND the call the model emitted, and
 * nothing compares them. `prov_call_unsourced` checks that every numeric literal in a call TRACES
 * to a supplied value; it says nothing about the operation, so `v=d/t` answered as
 * `eval((49.0)/(150.0))` is perfectly clean and perfectly inverted. The dimensional gate cannot see
 * coefficient, sign, orientation or term omission -- docs/WHAT_CHECKS_DO_NOT_VERIFY.md sweeps the
 * classes and lists all four. So the system's headline failure mode -- a confidently wrong,
 * well-formed, fully-sourced answer -- is undetected, and it is the exact failure the tool-augmented
 * architecture exists to prevent.
 *
 * THE RULE. Substitute the bound values into the record's right-hand side. That is the expression
 * the call MUST be. Canonicalise both as trees and compare.
 *
 *   * Flatten and sort `*` and `+`. They are associative and commutative, so `(9.81)*(5.0)*(2.0)`
 *     MUST PASS against `m*g*h`. A check that demanded source order would reject correct calls, and
 *     an oracle that fails on valid input is a worse instrument than the thing it measures.
 *   * Do NOT flatten or sort `/`, `-`, `^`. Order is meaning: `d/t` and `t/d` must differ. This is
 *     the clause that catches inversion, which provenance cannot see.
 *
 * THREE RESULTS, NOT TWO. `UNCHECKED` is distinct from `OK` on purpose: "cannot check" and "checked
 * and clean" must never share an exit status, which is a rule this project adopted after `dim_gate`
 * reported nothing wrong for three records whose units map had been invalidated. A caller that
 * treats UNCHECKED as a pass has reintroduced the defect.
 *
 * WHAT IT DOES NOT VERIFY, stated beside what it does:
 *   - that the record is the RIGHT record            -> that is `fit:`, a separate lookup
 *   - that the bound VALUES are correct              -> value entry is the student's step under E
 *   - that the RELATION is physically correct        -> dim_gate cannot see coefficients
 *   - that the FUNCTION chosen was right             -> this checks the expression, not the name
 *   - a COMMUTATIVE mis-binding: m=5,h=2 swapped in `m*g*h` is structurally AND numerically
 *     identical. Invisible, and harmless. In a non-commutative position it is caught.
 *   - that the PROSE is true                         -> provenance covers the answer span
 */

enum {
    TLM_SHAPE_OK        = 0,   /* the call is the relation, under the supplied bindings */
    TLM_SHAPE_MISMATCH  = 1,   /* it is not, and `why` says how */
    TLM_SHAPE_UNCHECKED = 2    /* NOT a pass. See `why`. */
};

/* `formula`  the record's relation, e.g. "U=m*g*h"
 * `var/val`  the bindings the runtime supplied, question givens AND record constants
 * `span`     the emitted call, e.g. "<tool>eval<arg>(2.0)*(5.0)</tool>"
 * `why`      filled on MISMATCH and on UNCHECKED; empty on OK. Never left stale.
 */
int tlm_shape_check(const char *formula,
                    const char *const *var, const char *const *val, int nvals,
                    const char *span, char *why, int cap);

/* THE ENTRY POINT ALL THREE CALLERS USE: the device generation loop, tools/eval/grade.py and
 * train/select_run.py (both through tools/eval/shapecli). Takes one full document -- prompt +
 * generation, the same string provcli takes -- reads the relation out of the <r> span and the
 * bindings out of the `name = number` clauses before <a>, and checks EVERY tool span in it.
 *
 * An earlier draft of this header declared a tlm_shape_check_rec() taking the store structs, and
 * nothing implemented it. A declaration with no definition reads as a feature and is not one --
 * the same shape as the IN_SCROLL handler that had no producer. Removed rather than left standing. */
int tlm_shape_check_doc(const char *full_document, char *why, int cap);
#endif
