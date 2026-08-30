#ifndef NS_PICKER_H
#define NS_PICKER_H
#include "loader.h"
#define NS_MAX_FAMILIES 24
/* Kept for ns_family callers. The PICKER's page size is PK_ROWS in pickui.h, which is
 * derived from draw_picker's real geometry; this 15 was an estimate and was 2 too many. */
#define NS_FAMILY_ROWS  15

typedef struct { const char *name; int count; } ns_family;

/* Families are derived from the UNIT of the solved-for variable through a fixed unit->family map.
 * A map over ~30 units, NOT a hand-labelled taxonomy over 166 records: a new record assigns its
 * own family, so the taxonomy cannot drift from the store. All 166 records map; no Other bucket. */
int  ns_family_of(const ns_store2 *st, int rec_index);      /* -1 if unmapped */
int  ns_families(const ns_store2 *st, ns_family *out, int max);
int  ns_records_in_family(const ns_store2 *st, int fam, int *out, int max);
/* Case-insensitive substring filter over record NAMES. scope<0 filters all records. */
int  ns_filter(const ns_store2 *st, int scope_fam, const char *q, int *out, int max);
const char *ns_family_name(int fam);
/* The family a UNIT belongs to, -1 if unmapped. Exposed because a record's variables carry
 * quantity information its NAME does not: "Hooke's law" contains no word a student asking for a
 * force would type, but its F is in newtons and the newton family is called "Force & pressure". */
int  ns_family_of_unit(const char *unit);
/* The nouns a student would use for a quantity in this unit, space-separated, or 0. Finer than the
 * family map, which is too coarse to retrieve with -- "Electricity & magnetism" contains neither
 * "current" nor "resistance". */
const char *ns_unit_nouns(const char *unit);
int  ns_family_count(void);
#endif
