/* A88 / A125 / A156. Which record does the APP send for a typed question -- or does it decline?
 *
 * WHY THIS SUITE EXISTS AT ALL. ask_confident is measured in test_askparse against the shipped
 * store, and that proves the predicate. It does not prove the app CONSULTS it -- and this repo has
 * shipped a written, unit-tested, uncalled check before (provenance.c, with no main and no caller,
 * reading as coverage the whole time) and an event handler with no producer (IN_SCROLL).
 *
 * THE OBSERVABLE IS THE RECORD ID SENT, NOT PICK_ON. This suite used to assert PICK_ON: 0 meant
 * "answered directly", 1 meant "the student chooses". A125 removed the picker from the answer path
 * -- a question the app is not confident about is now sent as Form C, a decline -- so PICK_ON is 0
 * on BOTH paths, every "answered directly" assertion here passed whatever the app did, and the
 * three "student still chooses" assertions failed. Nobody saw either, because run_gates.sh never
 * ran this binary (A156 found it built by `make tests` and listed nowhere in the gate loop, with
 * test_askparse and test_ansmatch). The host stub for app_request records the rid it was sent: a
 * record id is an answer path, the empty string is Form C.
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"

extern char HS_LASTQ[512], HS_LASTR[64];
extern int HS_NREQ;

static int F;
#define OK(cond, ...) do { int _c = !!(cond); if (!_c) F++; \
    printf("  %s  ", _c ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); } while (0)

/* Drive the real entry point: fill the compose buffer and press enter, as a keypress would.
 * Returns the formula of the record sent, "" for Form C, or 0 when nothing was sent at all. */
static const char *sent_for(const char *q) {
    int before = HS_NREQ;
    snprintf(COMPOSE, sizeof COMPOSE, "%s", q);
    COMPOSE_N = (int)strlen(COMPOSE);
    open_picker();
    if (HS_NREQ != before + 1) return 0;
    if (!HS_LASTR[0]) return "";
    const ns_store2 *st = app_store();
    for (int r = 0; r < st->n; r++)
        if (st->rec[r].rid && !strcmp(st->rec[r].rid, HS_LASTR))
            return st->rec[r].formula ? st->rec[r].formula : st->rec[r].name;
    return "?unknown rid";
}

static void answers(const char *q, const char *want) {
    const char *f = sent_for(q);
    if (want) OK(f && !strcmp(f, want), "sends %s: \"%.56s\" (got %s)", want, q, f ? (*f ? f : "Form C") : "nothing");
    else      OK(f && *f, "sends a record: \"%.56s\" (got %s)", q, f ? (*f ? f : "Form C") : "nothing");
    OK(PICK_ON == 0, "no picker shown (A125)");
}

static void declines(const char *q) {
    const char *f = sent_for(q);
    OK(f && !*f, "declines (Form C): \"%.56s\" (got %s)", q, f ? (*f ? f : "Form C") : "nothing");
    OK(PICK_ON == 0, "no picker shown (A125)");
}

int main(void) {
    gfx_init();
    app_init();
    if (!app_store()) {
        printf("CANNOT CHECK: build/store.tns did not load. Not a pass.\n");
        return 2;
    }
    printf("store loaded: %d records\n\n", app_store()->n);

    printf("-- the six device-transcript questions are answered ------------------------\n");
    const char *DIRECT[] = { "what is hookes law", "newton second law", "explain hokes law",
                             "what is kinetic energy", "find work when f=12 d=2.5",
                             "what is acceleration" };
    for (unsigned i = 0; i < sizeof DIRECT / sizeof DIRECT[0]; i++) answers(DIRECT[i], 0);

    printf("\n-- a glossary term is a lookup too ----------------------------------------\n");
    answers("what is entropy", 0);
    answers("what is a photon", 0);

    printf("\n-- A124: the question carries the relation ---------------------------------\n");
    answers("Solve F=m*a for a.", "F=m*a");

    printf("\n-- A155/A156: values typed as assignments ------------------------------------\n");
    answers("Given d = 150, t = 12, find the speed", "v=d/t");
    answers("Given d = 150 m, t = 12 s, find v", "v=d/t");              /* units are not symbols */
    answers("Using I_P = 8.54, N_P = 780, N_S = 2430, find I_S.", "I_S=((N_P)/(N_S))*I_P");

    /* ---- THE OTHER DIRECTION, which is the half that can silently rot --------------------- */
    printf("\n-- these must be declined ------------------------------------------------\n");
    declines("A sled is pushed 84 m in 7 s at constant speed. Find the speed.");  /* word problem */
    declines("Find the gradient of f(x,y,z) = xy + yz + xz at point P(1,2,3).");  /* out of scope */
    declines("Suppose P = 464, A = 3.39e-05. Compute v_d.");     /* binds I = P/A, asks for v_d  */
    declines("Take m = 497, v = 71.7, h = 3. What is K?");       /* binds lambda, asks for K     */
    declines("Take m = 497, v = 71.7. What is K?");              /* binds K AND p: ambiguous     */

    printf("\n-- neither path may leave the question in the compose buffer ---------------\n");
    sent_for("what is hookes law");
    OK(COMPOSE[0] == 0, "compose cleared on the answer path");
    sent_for("A sled is pushed 84 m in 7 s at constant speed. Find the speed.");
    OK(COMPOSE[0] == 0, "compose cleared on the decline path");

    printf("\n%s: %d failure%s\n", F ? "FAILED" : "PASS", F, F == 1 ? "" : "s");
    return F ? 1 : 0;
}
