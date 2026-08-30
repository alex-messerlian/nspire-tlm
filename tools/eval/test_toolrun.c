/* The DEVICE's tool path, verified on the host against the same evaluator the host cli uses.
 *
 * Until now run_call() returned "!give" and the number in the answer came from the model, so the
 * calculator ASSERTED the tool-augmented architecture rather than demonstrating it. This checks the
 * runtime half of that contract -- find the completed call, execute it, build the result span --
 * using the exact source that ships in chattlm.tns.
 *
 * WHAT THIS DOES NOT VERIFY: that ARMv5TE soft-float produces byte-identical results to this arm64
 * host. That is a different claim and tools/eval/device_main.c is the suite for it; it has to run
 * on hardware. What this establishes is that the runtime plumbing is right, so when the device
 * suite runs, a disagreement means arithmetic and not wiring.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/toolrun.h"

static int F;
static void T(const char *n, const char *got, const char *want) {
    int ok = strcmp(got, want) == 0;
    if (!ok) F++;
    printf("  %s  %-44s -> %s%s\n", ok ? "PASS" : "FAIL", n, got, ok ? "" : "");
    if (!ok) printf("        want %s\n", want);
}

int main(void) {
    char span[320], out[320];

    printf("\n  -- extracting the completed call --\n");
    struct { const char *doc, *want; } E[] = {
      { "<q>x</q><r>y</r><tool>eval<arg>150/12</tool>",            "<tool>eval<arg>150/12</tool>" },
      { "<tool>solve<arg>2x^2+3x-5=0<arg>x</tool>",                "<tool>solve<arg>2x^2+3x-5=0<arg>x</tool>" },
      /* two calls: the one that just closed is the LAST, not the first */
      { "<tool>eval<arg>1+1</tool><res>2</res><tool>eval<arg>3*3</tool>", "<tool>eval<arg>3*3</tool>" },
      /* still streaming -- no complete span yet, must not execute a half call */
      { "<tool>eval<arg>150/1",                                    "" },
      { "no call here at all",                                     "" },
    };
    for (unsigned i = 0; i < sizeof E / sizeof E[0]; i++) {
        tlm_extract_call(E[i].doc, span, sizeof span);
        T(E[i].doc, span, E[i].want);
    }

    printf("\n  -- executing, and the span fed back in --\n");
    struct { const char *call, *want; } R[] = {
      { "<tool>eval<arg>150/12</tool>",              "<res>12.5</res>" },
      { "<tool>eval<arg>(2.0)*(2.0)</tool>",         "<res>4</res>" },
      { "<tool>eval<arg>2*9.8*5</tool>",             "<res>98</res>" },
      /* refusals come back as the evaluator's own code, never as an invented number */
      { "<tool>eval<arg>1/0</tool>",                 "<res>!domain</res>" },
      { "<tool>nosuchfn<arg>1</tool>",               "<res>!name</res>" },
      /* WHAT THE MODEL ACTUALLY EMITS. The corpus is `<tool>eval` with no space -- 16,459 of
       * 16,459 sampled -- but the model emits TOKENS, and the tokenizer's decode reintroduces a
       * leading space. dispatch.c trimmed the args and not the name, so this returned !name on
       * device and the model invented "-2.4 J" from the poisoned result. */
      { "<tool> eval<arg> 0.5*(2.0)*((3.0))^(2)</tool>", "<res>9</res>" },
      { "<tool> eval<arg> (((400.0))/((0.02)))</tool>",  "<res>20000</res>" },
      { "<tool>  eval  <arg> 1+1</tool>",            "<res>2</res>" },
      /* and an unknown name must still fail, trimmed or not */
      { "<tool> nosuchfn<arg> 1</tool>",             "<res>!name</res>" },
      { "<tool>eval<arg>((((</tool>",                "<res>!parse</res>" },
      { "",                                          "<res>!give</res>" },
    };
    for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) {
        tlm_result_span(R[i].call, out, sizeof out);
        T(R[i].call[0] ? R[i].call : "(empty call)", out, R[i].want);
    }

    printf("\n  -- the contract: a refusal is reported as a refusal --\n");
    {   int ok = tlm_result_span("<tool>eval<arg>1/0</tool>", out, sizeof out);
        int okk = tlm_result_span("<tool>eval<arg>6*7</tool>", out, sizeof out);
        printf("  %s  1/0 reports failure (%d) and 6*7 reports success (%d)\n",
               (!ok && okk) ? "PASS" : "FAIL", ok, okk);
        if (ok || !okk) F++; }

    /* MUTATION: a runtime that fell back to the model's own text instead of the evaluator's code
     * is exactly the defect this replaces -- prove the oracle would catch it. */
    printf("\n  -- the status label a reader sees --\n");
    {   struct { const char *span, *want; } L[] = {
          { "<tool>eval<arg>150/12</tool>",             "eval(150/12)" },
          { "<tool>solve<arg>2x^2+3x-5=0<arg>x</tool>", "solve(2x^2+3x-5=0, x)" },
          { "<tool>conv<arg>5<arg>km<arg>m</tool>",     "conv(5, km, m)" },
          { "<tool>give</tool>",                        "give" },
          { "not a call",                               "eval" },
        };
        char lbl[64];
        for (unsigned i = 0; i < sizeof L / sizeof L[0]; i++) {
            tlm_call_label(L[i].span, lbl, sizeof lbl);
            T(L[i].span, lbl, L[i].want);
        }
        /* a span far longer than the buffer must truncate, not overrun */
        char tiny[12];
        tlm_call_label("<tool>eval<arg>1234567890123456789012345</tool>", tiny, sizeof tiny);
        int ok = strlen(tiny) < sizeof tiny;
        if (!ok) F++;
        printf("  %s  a long call truncates into a small buffer (\"%s\")\n", ok ? "PASS" : "FAIL", tiny); }

    printf("\n  -- mutation pass --\n");
    {   const char *faked = "<res>12.5</res>";      /* what a fabricating runtime would emit for 1/0 */
        tlm_result_span("<tool>eval<arg>1/0</tool>", out, sizeof out);
        int caught = strcmp(out, faked) != 0;
        if (!caught) F++;
        printf("  %s  mutant: a runtime inventing a value for 1/0 would fail this\n",
               caught ? "CAUGHT" : "MISSED"); }
    {   char s[320];
        tlm_extract_call("<tool>eval<arg>150/1", s, sizeof s);
        int caught = s[0] == 0;                     /* must NOT execute a half-streamed call */
        if (!caught) F++;
        printf("  %s  mutant: executing an unterminated call is refused\n",
               caught ? "CAUGHT" : "MISSED"); }

    printf("\n  %s: device tool path, %d failure(s)\n\n", F ? "FAIL" : "PASS", F);
    return F != 0;
}
