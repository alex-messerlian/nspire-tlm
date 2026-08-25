#ifndef NS_ASSEMBLE_H
#define NS_ASSEMBLE_H
#include "loader.h"
#define NS_PROMPT_MAX 1024
/* Values the student entered: parallel arrays, nvals entries. A variable absent from `var`
 * is MISSING -- absence is explicit, never inferred from an empty string. */
typedef struct { const char *var[NS_MAX_VARS2]; const char *val[NS_MAX_VARS2]; int nvals; } ns_input;

/* Assemble the prompt for a PICKED record. Returns length, or -1 on overflow/bad args.
 * fit is ALWAYS high: the student picked it, nothing is computed. */
int ns_assemble(char *out, int cap, const ns_rec2 *r, const char *question, const ns_input *in);

/* Form C: no record picked. A RUNTIME STRING, not a model behaviour -- measured 100% refused,
 * 0% confident answers on 88 real questions the store cannot serve. */
int ns_assemble_none(char *out, int cap, const char *question);
#endif
