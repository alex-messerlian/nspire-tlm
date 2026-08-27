/* test_shapecheck.c -- the structural call check, docs/ARCHITECTURE.md section 6.
 *
 * The nine cases from tools/eval/shape_spec.py, which decided the rule BEFORE it was written into
 * the document, plus the ones a Python prototype could not reach: UNCHECKED as a distinct status,
 * the unbound-variable path that is the mgh case, and the functions that have no shape rule yet.
 *
 * Every REJECT case also asserts WHICH reason fired. A check that rejects for the wrong reason will
 * reject the wrong things later, and the reason string is what a retry policy and an operator both
 * read. */
#include <stdio.h>
#include <string.h>
#include "shapecheck.h"

static int fails = 0, ran = 0;

static const char *S(int r) {
    return r == TLM_SHAPE_OK ? "OK" : r == TLM_SHAPE_MISMATCH ? "MISMATCH" : "UNCHECKED";
}

/* want: expected status. frag: a substring the reason must contain, or NULL to not check it. */
static void T(const char *label, const char *formula,
              const char *const *var, const char *const *val, int nvals,
              const char *span, int want, const char *frag) {
    char why[256];
    int got = tlm_shape_check(formula, var, val, nvals, span, why, sizeof why);
    ran++;
    int ok = (got == want) && (!frag || strstr(why, frag));
    if (!ok) {
        fails++;
        printf("  FAIL  %s\n        %s  call %s\n        got %s (want %s)\n        why: \"%s\"\n",
               label, formula, span, S(got), S(want), why);
        if (frag && !strstr(why, frag)) printf("        expected the reason to contain: \"%s\"\n", frag);
    } else {
        printf("  PASS  %-52s %-9s %s\n", label, S(got), why);
    }
    /* Invariant, checked on every single case: OK must never carry a reason, and the other two
     * must always carry one. A silent MISMATCH is unactionable and a chatty OK is a bug. */
    if (got == TLM_SHAPE_OK && why[0]) { fails++; printf("  FAIL  %s: OK carried a reason\n", label); }
    if (got != TLM_SHAPE_OK && !why[0]) { fails++; printf("  FAIL  %s: %s carried no reason\n", label, S(got)); }
}

int main(void) {
    printf("test_shapecheck\n");

    /* ---- the nine spec cases, ported verbatim from shape_spec.py ---------------------------- */
    {   const char *v[] = {"m","g","h"}; const char *x[] = {"2.0","9.81","5.0"};
        T("mgh, the observed failure", "U=m*g*h", v, x, 3,
          "<tool>eval<arg>(2.0)*(5.0)</tool>", TLM_SHAPE_MISMATCH, "9.81");
        T("mgh, correct", "U=m*g*h", v, x, 3,
          "<tool>eval<arg>(2.0)*(9.81)*(5.0)</tool>", TLM_SHAPE_OK, 0);
        T("mgh, reordered -- * is commutative, MUST PASS", "U=m*g*h", v, x, 3,
          "<tool>eval<arg>(9.81)*(5.0)*(2.0)</tool>", TLM_SHAPE_OK, 0);
        T("mgh, re-associated -- * is associative, MUST PASS", "U=m*g*h", v, x, 3,
          "<tool>eval<arg>((2.0)*(9.81))*(5.0)</tool>", TLM_SHAPE_OK, 0);
    }
    {   const char *v[] = {"d","t"}; const char *x[] = {"150.0","49.0"};
        T("v=d/t, the device E2E failure", "v=d/t", v, x, 2,
          "<tool>eval<arg>(150.0)*(150.0)</tool>", TLM_SHAPE_MISMATCH, "needs 49");
        T("v=d/t, correct", "v=d/t", v, x, 2,
          "<tool>eval<arg>(150.0)/(49.0)</tool>", TLM_SHAPE_OK, 0);
        T("v=d/t, INVERTED -- provenance cannot see this", "v=d/t", v, x, 2,
          "<tool>eval<arg>(49.0)/(150.0)</tool>", TLM_SHAPE_MISMATCH, "different structure");
    }
    {   const char *v[] = {"d","t"}; const char *x[] = {"240","30"};
        T("feedback.jsonl trace: fabricated operand", "v=d/t", v, x, 2,
          "<tool>eval<arg>(240.0)*(13.0)</tool>", TLM_SHAPE_MISMATCH, "needs 30");
    }
    {   const char *v[] = {"m","v"}; const char *x[] = {"4.0","3.0"};
        T("KE, coefficient dropped -- dim_gate cannot see this", "KE=0.5*m*v^2", v, x, 2,
          "<tool>eval<arg>(4.0)*(3.0)^2</tool>", TLM_SHAPE_MISMATCH, "0.5");
        T("KE, correct", "KE=0.5*m*v^2", v, x, 2,
          "<tool>eval<arg>0.5*(4.0)*(3.0)^2</tool>", TLM_SHAPE_OK, 0);
    }

    /* ---- what a Python prototype could not reach -------------------------------------------- */

    /* THE mgh CASE AS THE RUNTIME ACTUALLY SEES IT TODAY: g is never bound, because ns_assemble
     * drops the record's cval. The check must report UNCHECKED -- it cannot say what the call
     * should be -- and must NOT report OK. This is the case that makes the third status load-
     * bearing rather than tidy. */
    {   const char *v[] = {"m","h"}; const char *x[] = {"2.0","5.0"};
        T("mgh with g UNBOUND -- the live defect", "U=m*g*h", v, x, 2,
          "<tool>eval<arg>(2.0)*(5.0)</tool>", TLM_SHAPE_UNCHECKED, "no supplied value");
        T("mgh with g unbound, CORRECT call -- still unchecked, not clean", "U=m*g*h", v, x, 2,
          "<tool>eval<arg>(2.0)*(9.81)*(5.0)</tool>", TLM_SHAPE_UNCHECKED, "no supplied value");
    }

    /* Functions with no shape rule are UNCHECKED, never OK. */
    {   const char *v[] = {"m","g","h"}; const char *x[] = {"2.0","9.81","5.0"};
        T("diff has no shape rule -- unchecked, not clean", "U=m*g*h", v, x, 3,
          "<tool>diff<arg>m*g*h<arg>h</tool>", TLM_SHAPE_UNCHECKED, "no shape rule");
        T("integ has no shape rule -- unchecked, not clean", "U=m*g*h", v, x, 3,
          "<tool>integ<arg>m*g*h<arg>h<arg>0<arg>5</tool>", TLM_SHAPE_UNCHECKED, "no shape rule");
    }

    /* solve: argument 1 is the relation itself, unsubstituted. */
    {   const char *v[] = {"U","m","g"}; const char *x[] = {"147","3.0","9.81"};
        T("solve, correct relation", "U=m*g*h", v, x, 3,
          "<tool>solve<arg>U=m*g*h<arg>h</tool>", TLM_SHAPE_OK, 0);
        T("solve, reordered product in the relation", "U=m*g*h", v, x, 3,
          "<tool>solve<arg>U=h*g*m<arg>h</tool>", TLM_SHAPE_OK, 0);
        T("solve, WRONG relation", "U=m*g*h", v, x, 3,
          "<tool>solve<arg>U=m*g/h<arg>h</tool>", TLM_SHAPE_MISMATCH, "operator mismatch");
    }

    /* conv carries a target unit in argument 2; argument 1 is still the substituted expression. */
    {   const char *v[] = {"m","a"}; const char *x[] = {"12","9.8"};
        T("conv, correct, target unit ignored", "F=m*a", v, x, 2,
          "<tool>conv<arg>(12)*(9.8)<arg>N</tool>", TLM_SHAPE_OK, 0);
        T("conv, dropped operand", "F=m*a", v, x, 2,
          "<tool>conv<arg>(12)<arg>N</tool>", TLM_SHAPE_MISMATCH, "9.8");
    }

    /* Malformed input must be UNCHECKED or MISMATCH, never OK, and must never read out of bounds. */
    {   const char *v[] = {"d","t"}; const char *x[] = {"10","2"};
        T("no closing tag", "v=d/t", v, x, 2, "<tool>eval<arg>10/2", TLM_SHAPE_UNCHECKED, "complete call");
        T("empty argument",  "v=d/t", v, x, 2, "<tool>eval<arg></tool>", TLM_SHAPE_UNCHECKED, "empty");
        T("garbage argument","v=d/t", v, x, 2, "<tool>eval<arg>10/</tool>", TLM_SHAPE_MISMATCH, "does not parse");
        T("record with no '='","v", v, x, 2, "<tool>eval<arg>10/2</tool>", TLM_SHAPE_UNCHECKED, "no '='");
    }

    /* A sum: + is commutative too, and - is not. */
    {   const char *v[] = {"p_0","rho","g","h"}; const char *x[] = {"101325","1000","9.81","3"};
        T("p_0+rho*g*h, correct", "p=p_0+rho*g*h", v, x, 4,
          "<tool>eval<arg>(101325)+(1000)*(9.81)*(3)</tool>", TLM_SHAPE_OK, 0);
        T("p_0+rho*g*h, terms swapped -- + is commutative, MUST PASS", "p=p_0+rho*g*h", v, x, 4,
          "<tool>eval<arg>(1000)*(9.81)*(3)+(101325)</tool>", TLM_SHAPE_OK, 0);
        T("p_0+rho*g*h, MINUS instead of plus", "p=p_0+rho*g*h", v, x, 4,
          "<tool>eval<arg>(101325)-(1000)*(9.81)*(3)</tool>", TLM_SHAPE_MISMATCH, "operator mismatch");
        T("p_0+rho*g*h, term omitted", "p=p_0+rho*g*h", v, x, 4,
          "<tool>eval<arg>(1000)*(9.81)*(3)</tool>", TLM_SHAPE_MISMATCH, "needs 101325");
    }

    /* Tolerance: 9.8 for 9.81 is a DIFFERENT PREMISE and must be caught. */
    {   const char *v[] = {"m","g","h"}; const char *x[] = {"2.0","9.81","5.0"};
        T("9.8 substituted for 9.81 -- a different premise", "U=m*g*h", v, x, 3,
          "<tool>eval<arg>(2.0)*(9.8)*(5.0)</tool>", TLM_SHAPE_MISMATCH, "9.81");
        T("2 for 2.0 -- the same number, MUST PASS", "U=m*g*h", v, x, 3,
          "<tool>eval<arg>2*9.81*5</tool>", TLM_SHAPE_OK, 0);
    }

    /* THE DOCUMENTED BLIND SPOT, asserted so it cannot be lost: a commutative mis-binding is
     * invisible, and it is also numerically harmless. If this ever starts failing, the check has
     * become stricter than the rule the document states. */
    {   const char *v[] = {"m","g","h"}; const char *x[] = {"5.0","9.81","2.0"};
        T("BLIND SPOT: m and h swapped in a product -- invisible by design", "U=m*g*h", v, x, 3,
          "<tool>eval<arg>(2.0)*(9.81)*(5.0)</tool>", TLM_SHAPE_OK, 0);
    }

    /* A FORMULA CONTAINING '|'. `f_beat=|f_2-f_1|` is a real store record. tlm_shape_check_doc read
     * the record span up to the FIRST '|' and extracted "f_beat=", making every absolute-value
     * relation permanently UNCHECKED. The three-valued result is what kept that from being a wrong
     * answer instead of a gap. These go through the DOCUMENT entry point, because the defect was in
     * the span parsing rather than in the rule. */
    {   const char *P = "<q>Two waves. f_2 = 300, f_1 = 260.</q><r>f_beat=|f_2-f_1| | "
                        "f_beat:Hz f_2:Hz f_1:Hz | missing:none | standard conditions | fit:high";
        char why[256];
        char d1[600], d2[600];
        snprintf(d1, sizeof d1, "%s<tool>eval<arg>|(300)-(260)|</tool><res>40</res><a> 40 Hz.<end>", P);
        snprintf(d2, sizeof d2, "%s<tool>eval<arg>(300)*(260)</tool><res>78000</res><a> 78000.<end>", P);
        int a = tlm_shape_check_doc(d1, why, sizeof why); ran++;
        if (a != TLM_SHAPE_OK) { fails++; printf("  FAIL  pipe in formula, correct call -> %s (%s)\n", S(a), why); }
        else printf("  PASS  %-52s %-9s\n", "pipe in formula, correct call", "OK");
        int b = tlm_shape_check_doc(d2, why, sizeof why); ran++;
        if (b != TLM_SHAPE_MISMATCH) { fails++; printf("  FAIL  pipe in formula, wrong call -> %s (%s)\n", S(b), why); }
        else printf("  PASS  %-52s %-9s %s\n", "pipe in formula, wrong call", "MISMATCH", why);
    }

    printf("test_shapecheck %s  (%d cases, %d failures)\n", fails ? "FAIL" : "PASS", ran, fails);
    return fails ? 1 : 0;
}
