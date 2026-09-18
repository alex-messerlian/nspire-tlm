/* A88. Does the app SKIP the picker when one record already answers the question?
 *
 * WHY THIS SUITE EXISTS AT ALL. ask_confident is measured in test_askparse against the shipped
 * store, and that proves the predicate. It does not prove the app CONSULTS it -- and this repo has
 * shipped a written, unit-tested, uncalled check before (provenance.c, with no main and no caller,
 * reading as coverage the whole time) and an event handler with no producer (IN_SCROLL). So the
 * assertion here is not about coverage percentages; it is about PICK_ON, the visible consequence.
 *
 * The flow is: open_picker() either sends the request straight through (PICK_ON == 0) or raises
 * the shortlist (PICK_ON == 1). Nothing else distinguishes them from outside.
 *
 * THE FALLBACK IS THE PICKER, NEVER A REFUSAL. A miss costs one screen, which is what every
 * question cost before this change, so the not-confident cases below assert "the student still
 * gets to choose" rather than "the device declines".
 */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"

static int F;
#define OK(cond, ...) do { int _c = !!(cond); if (!_c) F++; \
    printf("  %s  ", _c ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); } while (0)

/* Drive the real entry point: fill the compose buffer and open the picker, as a keypress would. */
static int picker_opened_for(const char *q) {
    PICK_ON = 0;
    snprintf(COMPOSE, sizeof COMPOSE, "%s", q);
    open_picker();
    return PICK_ON;
}

int main(void) {
    gfx_init();
    app_init();
    if (!app_store()) {
        printf("CANNOT CHECK: build/store.tns did not load. Not a pass.\n");
        return 2;
    }
    printf("store loaded: %d records\n\n", app_store()->n);

    /* ---- the device transcript: all six should go straight through ------------------------- */
    printf("-- the six device-transcript questions skip the picker --------------------\n");
    const char *DIRECT[] = { "what is hookes law", "newton second law", "explain hokes law",
                             "what is kinetic energy", "find work when f=12 d=2.5",
                             "what is acceleration" };
    for (unsigned i = 0; i < sizeof DIRECT / sizeof DIRECT[0]; i++)
        OK(picker_opened_for(DIRECT[i]) == 0, "answered directly: \"%s\"", DIRECT[i]);

    /* ---- a glossary lookup, which is why the knowledge tier is in the store ---------------- */
    printf("\n-- a glossary term is a lookup too ----------------------------------------\n");
    OK(picker_opened_for("what is entropy") == 0, "answered directly: \"what is entropy\"");
    OK(picker_opened_for("what is a photon") == 0, "answered directly: \"what is a photon\"");

    /* ---- THE OTHER DIRECTION, which is the half that can silently rot --------------------- */
    printf("\n-- these must still reach the picker --------------------------------------\n");
    const char *VIA_PICKER[] = {
        "A sled is pushed 84 m in 7 s at constant speed. Find the speed.",   /* word problem   */
        "Find the gradient of f(x,y,z) = xy + yz + xz at point P(1,2,3).",   /* out of scope   */
        "Solve F=m*a for a.",        /* one content word, fuzzy-matches "(solved version)"     */
    };
    for (unsigned i = 0; i < sizeof VIA_PICKER / sizeof VIA_PICKER[0]; i++)
        OK(picker_opened_for(VIA_PICKER[i]) == 1, "student still chooses: \"%.52s\"", VIA_PICKER[i]);

    /* ---- the compose buffer must not survive either path ----------------------------------- */
    printf("\n-- neither path may leave the question in the compose buffer ---------------\n");
    picker_opened_for("what is hookes law");
    OK(COMPOSE[0] == 0, "compose cleared on the direct path");
    picker_opened_for("A sled is pushed 84 m in 7 s at constant speed. Find the speed.");
    OK(COMPOSE[0] == 0, "compose cleared on the picker path");

    printf("\n%s: %d failure%s\n", F ? "FAILED" : "PASS", F, F == 1 ? "" : "s");
    return F ? 1 : 0;
}
