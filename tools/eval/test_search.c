/* Device search == web search. The web suite is the oracle: these are the SAME ten cases run
 * against the C implementation, plus a mutation pass proving the suite can fail.
 *
 * app.c is #included rather than linked because the search internals are static -- the alternative
 * is exporting them purely for the test, which widens the shipping surface for no benefit. */
#include <stdio.h>
#include <string.h>
#include "../../src/store/app.c"

static int FAILS;
static void seed(const char *title, const char *q, const char *a) {
    app_chat *c = &CHATS[NCHATS++];
    memset(c, 0, sizeof *c);
    snprintf(c->title, sizeof c->title, "%s", title);
    snprintf(c->turn[0].q, sizeof c->turn[0].q, "%s", q);
    snprintf(c->turn[0].a, sizeof c->turn[0].a, "%s", a);
    c->nturns = 1; c->used = 1;
}
/* hits as a comma list of 1-based ids, matching the web fixture numbering */
static const char *hits_str(void) {
    static char b[64]; int n = 0; b[0] = 0;
    for (int i = 0; i < NSHIT; i++) n += snprintf(b + n, sizeof b - n, "%s%d", i ? "," : "", SHIT[i] + 1);
    return b;
}
static void T(const char *name, const char *query, const char *want) {
    snprintf(SQ, sizeof SQ, "%s", query); SQ_N = (int)strlen(SQ); SSEL = 0;
    run_search();
    const char *got = hits_str();
    int ok = strcmp(got, want) == 0;
    if (!ok) FAILS++;
    printf("  %s  %-34s query=%-18s got=[%s]%s\n", ok ? "PASS" : "FAIL", name, query, got,
           ok ? "" : (printf(" want=[%s]", want), ""));
}

int main(void) {
    seed("A car goes 150 m in 12 s", "A car goes 150 m in 12 s. Find the speed.",
         "The speed is 12.5 m/s. This is a kinematics problem: velocity is displacement over elapsed time, v = d/t.");
    seed("3 A through a 4 ohm resistor", "3 A flows through 4 ohm. Find the voltage.",
         "The voltage is 12 V by Ohm law, V = IR. The resistor dissipates power as heat.");
    seed("A 2 kg mass raised 5 m", "A 2 kg mass is raised 5 m. Find the potential energy.",
         "The gravitational potential energy is 98 J from U = mgh with g = 9.8 m/s^2.");
    seed("Cart on a level track", "A 4 kg cart moves at 3 m/s. Find the kinetic energy.",
         "The kinetic energy is 18 J from K = mv^2/2. No potential energy changes on a level track.");

    printf("\n  -- content search, ten cases mirrored from the web suite --\n");
    T("body-only term",              "velocity",         "1");
    T("AND, neither term in title",  "kinetic energy",   "4");
    T("term in two sessions",        "potential energy", "3,4");
    T("term split title+body",       "ohm resistor",     "2");
    T("AND excludes",                "energy ohm",       "");
    T("absent term",                 "quantum",          "");
    T("whole word beats mid-word",   "car",              "1,4");
    T("exact word only",             "cart",             "4");
    T("empty query lists all",       "",                 "1,2,3,4");
    T("case insensitive",            "VELOCITY",         "1");

    printf("\n  -- match-kind weighting --\n");
    struct { const char *hay, *term; int want_kind; } K[] = {
        { "a car goes",  "car",  300 },   /* whole word  */
        { "cart on a",   "car",  150 },   /* word start  */
        { "oscar rides", "car",    0 },   /* mid-word    */
    };
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) {
        int s = term_score(K[i].hay, K[i].term, 1000);
        int pos_pen = (int)(strstr(K[i].hay, K[i].term) - K[i].hay) / 10;
        int kind = s - 1000 + pos_pen;
        int ok = kind == K[i].want_kind;
        if (!ok) FAILS++;
        printf("  %s  kind of \"%s\" in \"%s\" = %d (want %d)\n",
               ok ? "PASS" : "FAIL", K[i].term, K[i].hay, kind, K[i].want_kind);
    }

    printf("\n  -- snippet windows on the match, not the start of the message --\n");
    { char terms[MAX_TERMS][TERM_MAX]; char sn[110];
      int nt = split_terms("velocity", terms);
      snippet_of(&CHATS[0], terms, nt, sn, sizeof sn);
      int ok = strstr(sn, "velocity") != NULL && sn[0] == '.';
      if (!ok) FAILS++;
      printf("  %s  snippet=\"%s\"\n", ok ? "PASS" : "FAIL", sn); }

    /* MUTATION PASS: a suite that cannot fail is not a suite. Score title-only -- the exact defect
     * the web side had -- and confirm the body-only case stops matching. */
    printf("\n  -- mutation pass --\n");
    { char terms[MAX_TERMS][TERM_MAX];
      int nt = split_terms("velocity", terms), found = 0;
      for (int i = 0; i < NCHATS; i++)
          if (term_score(lowr(CHATS[i].title), terms[0], 1000) >= 0) found++;
      (void)nt;
      int ok = found == 0;
      if (!ok) FAILS++;
      printf("  %s  mutant title-only: 'velocity' matches %d sessions (must be 0)\n",
             ok ? "CAUGHT" : "MISSED", found); }
    { char terms[MAX_TERMS][TERM_MAX];
      int nt = split_terms("energy ohm", terms), or_hits = 0;
      for (int i = 0; i < NCHATS; i++) {
          int any = 0;
          for (int j = 0; j < nt; j++) {
              if (term_score(lowr(CHATS[i].title), terms[j], 1000) >= 0) any = 1;
              for (int k = 0; k < CHATS[i].nturns; k++) {
                  if (term_score(lowr(CHATS[i].turn[k].q), terms[j], 500) >= 0) any = 1;
                  if (term_score(lowr(CHATS[i].turn[k].a), terms[j], 500) >= 0) any = 1;
              }
          }
          or_hits += any;
      }
      int ok = or_hits > 0;
      if (!ok) FAILS++;
      printf("  %s  mutant OR-semantics: 'energy ohm' matches %d (AND says 0)\n",
             ok ? "CAUGHT" : "MISSED", or_hits); }

    /* RANKING, which the ten mirrored cases cannot see. Every one of them expects hits in
     * ascending index order, so removing the insertion sort in run_search() left all ten passing
     * -- a negative control survived here and that survival is what found this hole. A title hit
     * scores 1000 and a body hit 500, so a later session with the term in its TITLE must outrank
     * an earlier one that only mentions it. */
    printf("\n  -- ranking: a title hit outranks an earlier body hit --\n");
    seed("Velocity of a falling stone", "A stone falls for 3 s. Find the speed.",
         "It reaches 29.4 m/s.");
    T("title hit sorts above body hit", "velocity", "5,1");

    printf("\n  %s: device search, %d failure(s)\n\n", FAILS ? "FAIL" : "PASS", FAILS);
    return FAILS != 0;
}
