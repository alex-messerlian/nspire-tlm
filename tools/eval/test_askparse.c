/* The shipped picker/parser, driven on the host against the shipped store.
 *
 * Every case here is a question the DEVICE produced or would produce. Two of them are the
 * defects this file exists for, and both are asserted in the direction that failed:
 *
 *   SUBSTRING   "what is force" picked "Approximate time FOR exchange of a virtual particle",
 *               because "for" is a substring of "force". Now a whole-word match.
 *   DUPLICATION "what is force, k = 500, x = 0.4" assembled to
 *               "<q>what is force, k = 500, x = 0.4 k = 500, x = 0.4.</q>" -- ns_assemble appends
 *               the givens itself, so the question must not still carry them.
 *
 * Run: build/test_askparse build/store.tns
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../../src/store/askparse.h"

static int fails = 0, ran = 0;
static ns_store2 ST;
static ns_ask A;

static void ck(int ok, const char *what, const char *got) {
    ran++;
    if (!ok) { fails++; printf("  FAIL %-46s got: %s\n", what, got ? got : "(null)"); }
}

/* Assemble exactly as app_request does, so the test covers the seam and not just the pieces. */
static const char *assemble(const char *q) {
    static char prompt[NS_PROMPT_MAX];
    ask_build(&ST, q, &A);
    if (ns_assemble(prompt, sizeof prompt, &ST.rec[A.idx], A.question, &A.in) < 0) return "(overflow)";
    return prompt;
}

static void case_pick(const char *q, const char *want_lhs, int want_min_score) {
    ask_build(&ST, q, &A);
    const char *lhs = ST.rec[A.idx].lhs ? ST.rec[A.idx].lhs : "";
    char msg[512];
    snprintf(msg, sizeof msg, "lhs=%s score=%d name=%s", lhs, A.score,
             ST.rec[A.idx].name ? ST.rec[A.idx].name : "");
    ck(strcmp(lhs, want_lhs) == 0 && A.score >= want_min_score, q, msg);
}

static void case_formula(const char *q, const char *want) {
    ask_build(&ST, q, &A);
    const char *f = ST.rec[A.idx].formula ? ST.rec[A.idx].formula : "";
    char msg[512]; snprintf(msg, sizeof msg, "formula=%s score=%d", f, A.score);
    ck(strcmp(f, want) == 0, q, msg);
}

static void case_nvals(const char *q, int want) {
    ask_build(&ST, q, &A);
    char msg[256]; snprintf(msg, sizeof msg, "nvals=%d", A.in.nvals);
    ck(A.in.nvals == want, q, msg);
}

/* COUNT the value, do not pattern-match the gap around it.
 *
 * This was `case_absent(q, "0.4 k = 500")` -- a literal that only appears when the duplicated
 * values sit adjacent. The moment ns_assemble started closing the question before the givens
 * ("... x = 0.4. Given k = 500, ...") the string stopped occurring, the control SURVIVED, and the
 * duplication it guards would have shipped again. A count is what the property actually is, and
 * no change of separator can defeat it. */
static int count_of(const char *hay, const char *needle) {
    int n = 0; size_t L = strlen(needle);
    for (const char *p = hay; (p = strstr(p, needle)); p += L) n++;
    return n;
}
static void case_once(const char *q, const char *val) {
    const char *p = assemble(q);
    int c = count_of(p, val);
    char msg[1100]; snprintf(msg, sizeof msg, "%d occurrences of \"%s\" in %s", c, val, p);
    ck(c == 1, q, msg);
}

static void case_contains(const char *q, const char *must_contain) {
    const char *p = assemble(q);
    char msg[1100]; snprintf(msg, sizeof msg, "%s", p);
    ck(strstr(p, must_contain) != 0, q, msg);
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "build/store.tns";
    int rc = ns_load(&ST, path);
    if (rc != NS_OK) { printf("load %s: %s\n", path, ns_strerror(rc)); return 2; }
    printf("store %s: %d records\n\n", path, ST.n);

    printf("-- picking ------------------------------------------------------------\n");
    /* THE REGRESSION. Substring matching sent this to a virtual-particle record. */
    case_pick("what is force, k = 500, x = 0.4", "F", 1);
    case_pick("hooke's law k = 500, x = 0.4", "F", 2);
    case_pick("kinetic energy m = 2, v = 3", "K", 2);
    /* THE SUBSTRING CASE, and it took a control to find one that works. My first pick,
     * "what is force, k = 500, x = 0.4", stopped distinguishing the two the moment supplied
     * variables entered the score -- so a revert to substring matching left the suite green and
     * the defect back. This one has no values at all: substring matching sends it to
     * Delta_t=((h)/(E)), "Approximate time FOR exchange of a virtual particle", on "for" inside
     * "force". Measured: the two rankers disagree on 31 of 200 labelled items. */
    case_pick("A 400 N force acts over 0.02 m^2. Find the pressure.", "p", 1);
    /* THE LEADING boundary specifically. "length" is a SUFFIX of "wavelength", so without the
     * check on the character BEFORE the match this reaches a resonance record via a word the
     * question never uses. The trailing check cannot see it -- the match already ends at a
     * boundary. Two boundaries, two controls: one control per condition. */
    case_formula("A 240 Hz wave travels at 343 m/s. Find its wavelength.", "v=((lambda)/(T))");
    /* No name term matches at all -> record 0, score 0. The fallback must be VISIBLE (score 0),
     * not indistinguishable from a real 1-word hit, which is what made the first version wrong. */
    ask_build(&ST, "zzzz qqqq", &A);
    { char m[64]; snprintf(m, sizeof m, "score=%d idx=%d", A.score, A.idx);
      ck(A.score == 0 && A.idx == 0, "no match -> record 0 with score 0", m); }

    printf("\n-- parsing ------------------------------------------------------------\n");
    case_nvals("what is force, k = 500, x = 0.4", 2);
    case_nvals("m = 2, v = 3", 2);
    case_nvals("c = 3.0e8", 1);
    case_nvals("t = 3.71e-07 and d = -12.5", 2);
    case_nvals("what is force", 0);
    case_nvals("is x = y a thing", 0);            /* '=' with no number is not a given */

    printf("\n-- the question must not carry the values twice ------------------------\n");
    /* THE REGRESSION. Each value present EXACTLY ONCE, in the list ns_assemble builds. */
    case_once("what is force, k = 500, x = 0.4", "k = 500");
    case_once("what is force, k = 500, x = 0.4", "x = 0.4");
    case_contains("what is force, k = 500, x = 0.4", "<q>what is force. Given ");
    case_contains("what is force, k = 500, x = 0.4", "missing:none");
    /* A question that is ONLY values must not strip down to nothing. */
    ask_build(&ST, "k = 500, x = 0.4", &A);
    ck(A.question && A.question[0], "values-only question survives stripping", A.question);

    printf("\n-- A52: removing the values must not leave the words that introduced them ---\n");
    /* THE REGRESSION. "find the kinetic energy when you have m=900 and v=800" stripped to
     * "find the kinetic energy when you have and", and ns_assemble appended a period, so the model
     * was handed "...when you have and." -- a student's exact phrasing made ungrammatical by our
     * own stripper. Found by running the planned DEVICE turns on the host; no gate reads a
     * question, so nothing else could have caught it. */
    case_contains("find the kinetic energy when you have m=900 and v=800",
                  "<q>find the kinetic energy. Given ");
    case_contains("kinetic energy where m = 5 and v = 3", "<q>kinetic energy. Given ");
    case_contains("work with F = 12 and d = 2.5", "<q>work. Given ");
    /* THE CONTROL, in the other direction: the trim walks only a TRAILING run and must stop at the
     * first content word. A question whose real text ends in a content word is untouched, and
     * "when you have" INSIDE a sentence is not a trailing run at all. */
    case_contains("A 3.0 kg block accelerates. What net force?", "What net force?");
    ask_build(&ST, "m = 2, v = 3", &A);
    ck(A.question && A.question[0], "a values-only question still survives the trim", A.question);

    printf("\n%s  %d/%d\n", fails ? "FAIL" : "PASS", ran - fails, ran);
    return fails ? 1 : 0;
}
