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

    printf("\n-- A54: a possessive record name is reachable without the apostrophe ------\n");
    /* MEASURED ON DEVICE: the user could not find the apostrophe on the keypad, typed
     * "what is hookes law", and the picker returned LAW OF REFRACTION -- then the model explained
     * Snell's law confidently and one sample fabricated a number. "Hooke's law" splits into
     * "hooke" and "law" (the lone "s" is under the length floor), so "hookes" matched neither. */
    /* A64. NOW ASSERTED THROUGH ask_build, and the comment this replaces is worth keeping in
     * mind. It read: "NOT asserted here, and the reason is a finding: THERE ARE TWO RANKERS. The
     * device shows pk_open's SHORTLIST ... ask_build's auto-pick still ties on the bare two-word
     * form ... recorded in docs/RESULT_DEVICE_TEST_1.md rather than papered over with an
     * expectation that matches whichever path I happened to call."
     *
     * That was the right call at the time and it rested on one premise: that a human picks from
     * pk_open's shortlist, so the auto-pick's answer does not reach the student. The user is
     * REMOVING the suggestion UI, which makes ask_build's auto-pick the only path, and the premise
     * dies with it.
     *
     * ask_pick is now an argmax over score_record -- the same function ask_rank ranks with, in the
     * same ASK_NOUN mode pk_open uses -- so there is one scorer behind two entry points and they
     * agree on 656 of 656 name-bearing questions. These lines can therefore assert the path the
     * user sees, which is what the old comment said it wanted and could not have. */
    case_formula("what is hookes law", "F=-k*x");      /* no apostrophe: the device test's exact input */
    case_formula("hookes law", "F=-k*x");
    case_formula("explain hookes law", "F=-k*x");
    case_formula("whats hooks law", "F=-k*x");         /* and misspelt, which needs A56 or A54 */
    case_formula("what is Hooke's law", "F=-k*x");     /* the apostrophe form still works */
    /* A67 CHANGED THIS EXPECTATION, and it is changed because the DEVICE TEST called the old one
     * wrong, not because the new ranker happens to produce it. The store carries two records:
     *
     *     F=m*a      "Newton's second law"
     *     F_net=m*a  "Newton's second law, scalar form"
     *
     * and the user typed "newton second law" on the calculator and reported back: "newton second
     * law first suggested the scalar version or smth". The bare name is the exact name of F=m*a;
     * the other carries a qualifier the question did not ask for. Name coverage prefers the record
     * whose name IS the query over one that merely contains it, which is the whole point of the
     * term, and this is the case that says so. */
    case_formula("newtons second law", "F=m*a");
    case_formula("newton second law", "F=m*a");        /* the device transcript's exact input */
    case_formula("what is newtons second law", "F=m*a");
    /* THE CONTROL IS MEASURED, NOT ASSERTED HERE, and the first version of this line got that
     * wrong: I asserted "A runner averages 5.5 m/s for 1320 s. How far?" should return v=d/t. It
     * never did -- before or after the change -- so the assertion described a behaviour that has
     * never existed and failed for that reason rather than finding anything.
     *
     * What IS measured, by rebuilding rankcli from a patched copy outside the tree and diffing
     * top-1 over all 200 items.json questions: relaxing word_in to accept ANY trailing 's' left
     * retrieval@1 unchanged at 18.4% and made SEVEN questions falsely match a record where nothing
     * had matched ("seconds", "lengths", "cables" became matches). The possessive-only form
     * changes 0 of 200 picks. A whole-benchmark diff is the right instrument for a ranking change;
     * a single hand-written expectation is not. */

    /* ---- ask_confident: BOTH DIRECTIONS, ON REAL QUESTIONS -------------------------------
     *
     * A66/A88. ask_pick cannot say "no match" -- it returns record 0, which is Hooke's law -- and
     * a threshold on its SCORE cannot fix that (measured: the best cut refuses 49.2% of certified
     * out-of-scope questions while keeping 68.0% of in-scope ones). ask_confident asks a different
     * question: how much of the QUESTION does the winner explain.
     *
     * A CONTROL PER DIRECTION, because a predicate that always says yes and a predicate that works
     * produce the same output on the confident cases alone.
     *
     * The six CONFIDENT cases are the device transcript verbatim -- the questions the student
     * actually typed -- and not synthetic ones: a control built on a made-up record tests the
     * mechanism and not the property.
     *
     * A124 MOVED THE TWO REARRANGEMENT CASES FROM NOT-CONFIDENT TO CONFIDENT, and the reason is a
     * different signal rather than a relaxed threshold. "Solve F=m*a for a." is the case that made
     * this measurement honest: it reduces to a single content word, "solve", which fuzzy-matches
     * the record named "lens/mirror equation (SOLVED version)", and under full-weight fuzzy
     * matching it read as a 100%-explained question. That failure mode is still real and is still
     * what ask_qcover must not do -- if the fuzzy half-weight is ever removed, coverage breaks
     * again. What changed is that the question CARRIES THE RELATION: "F=m*a" is the record's own
     * formula, typed by the student, and question_carries_relation now says so directly. Both
     * resolve to the RIGHT record, which the old path never did.
     *
     * Measured over the 2,000 certified out-of-scope stems, the two rules together are better on
     * BOTH axes than coverage alone: in-scope 73.3% -> 93.3%, false positives 2.2% -> 1.6%. A third
     * rule (a verbatim record name) reached 100% in-scope and 19.8% false positives and was
     * rejected; see askparse.c.
     *
     * The NOT-CONFIDENT cases that REMAIN are the two that must still refuse: an out-of-scope
     * question and a word problem. Those are the shapes where a record would be invented. */
    {   int idx;
        const char *CONF[] = { "what is hookes law", "newton second law", "explain hokes law",
                               "what is kinetic energy", "find work when f=12 d=2.5",
                               "what is acceleration",
                               /* A124: the question carries the relation, so it is certainty */
                               "Solve F=m*a for a.", "Solve v=d/t for d.",
                               "what is the derivative of 0.5*m*(v)^(2) with respect to v" };
        for (unsigned i = 0; i < sizeof CONF / sizeof CONF[0]; i++) {
            static ns_ask a; ask_parse(CONF[i], &a);
            int c = ask_confident(&ST, CONF[i], &a.in, &idx);
            char m[160]; snprintf(m, sizeof m, "confident=%d, coverage %d%%", c,
                                  idx >= 0 ? ask_qcover(&ST, idx, CONF[i]) : -1);
            ck(c == 1, CONF[i], m);
        }
        const char *NOPE[] = {
            "Find the gradient of f(x,y,z) = xy + yz + xz at point P(1,2,3).",  /* out of scope  */
            "A sled is pushed 84 m in 7 s at constant speed. Find the speed.",  /* word problem  */
        };
        for (unsigned i = 0; i < sizeof NOPE / sizeof NOPE[0]; i++) {
            static ns_ask a; ask_parse(NOPE[i], &a);
            int c = ask_confident(&ST, NOPE[i], &a.in, &idx);
            char m[160]; snprintf(m, sizeof m, "confident=%d, coverage %d%%", c,
                                  idx >= 0 ? ask_qcover(&ST, idx, NOPE[i]) : -1);
            ck(c == 0, NOPE[i], m);
        }
    }

    printf("\n%s  %d/%d\n", fails ? "FAIL" : "PASS", ran - fails, ran);
    return fails ? 1 : 0;
}
