/* Picker: family list -> record list -> filter. See docs/PICKER_SPEC.md.
 *
 * No allocation, no printf. Every function returns a count and fills a caller-provided array, so
 * a failure is a number the caller can test rather than a silence on a device with no console. */
#include <string.h>
#include <ctype.h>
#include "picker.h"

/* unit -> family. Order defines first-screen order. 13 families: measured worst browse path is
 * 27 records (2 screens) against 61 (5 screens) for a coarser 7-family split, at no cost --
 * 13 rows still fit the 15 visible. HEADROOM IS 2 ROWS: a 16th family would start scrolling. */
static const char *FAMILY[] = {
    "Distance, area & volume", "Speed & velocity", "Acceleration", "Time & period",
    "Force & pressure", "Energy & work", "Power", "Mass & momentum",
    "Electricity & magnetism", "Waves & optics", "Rotation",
    "Heat & thermodynamics", "Ratios & dimensionless",
};
#define NFAM ((int)(sizeof FAMILY / sizeof FAMILY[0]))

static const struct { const char *unit; int fam; } UNIT_FAM[] = {
    {"m",0},{"m^2",0},{"m^3",0}, {"m/s",1}, {"m/s^2",2}, {"s",3},
    {"N",4},{"N*m",4},{"N/m",4},{"Pa",4}, {"J",5}, {"W",6},
    {"kg",7},{"kg*m/s",7},{"kg/m^3",7},
    {"V",8},{"A",8},{"ohm",8},{"F",8},{"C",8},{"T",8},{"V/m",8},
    {"Hz",9},{"1/m",9},{"W/m^2",9},
    {"1/s",10},{"1/s^2",10},{"kg*m^2/s",10},{"kg*m^2",10},
    {"K",11},{"J/K",11},{"J/(mol*K)",11},
    {"1",12},
};
#define NUF ((int)(sizeof UNIT_FAM / sizeof UNIT_FAM[0]))

int ns_family_count(void) { return NFAM; }
const char *ns_family_name(int f) { return (f >= 0 && f < NFAM) ? FAMILY[f] : 0; }

int ns_family_of_unit(const char *unit) {
    if (!unit) return -1;
    for (int k = 0; k < NUF; k++) if (strcmp(UNIT_FAM[k].unit, unit) == 0) return UNIT_FAM[k].fam;
    return -1;
}

int ns_family_of(const ns_store2 *st, int i) {
    if (!st || i < 0 || i >= st->n) return -1;
    const ns_rec2 *r = &st->rec[i];
    const char *lu = 0;
    for (int k = 0; k < r->nvars; k++)
        if (r->var[k] && strcmp(r->var[k], r->lhs) == 0) { lu = r->unit[k]; break; }
    return ns_family_of_unit(lu);
}

int ns_families(const ns_store2 *st, ns_family *out, int max) {
    if (!st || !out || max < NFAM) return -1;
    for (int f = 0; f < NFAM; f++) { out[f].name = FAMILY[f]; out[f].count = 0; }
    for (int i = 0; i < st->n; i++) { int f = ns_family_of(st, i); if (f >= 0) out[f].count++; }
    return NFAM;
}

int ns_records_in_family(const ns_store2 *st, int fam, int *out, int max) {
    if (!st || !out || fam < 0 || fam >= NFAM) return -1;
    int n = 0;
    for (int i = 0; i < st->n && n < max; i++) if (ns_family_of(st, i) == fam) out[n++] = i;
    return n;
}

static int ci_contains(const char *hay, const char *needle) {
    if (!*needle) return 1;
    for (const char *p = hay; *p; p++) {
        const char *a = p, *b = needle;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*b) return 1;
    }
    return 0;
}

int ns_filter(const ns_store2 *st, int scope_fam, const char *q, int *out, int max) {
    if (!st || !out || !q) return -1;
    int n = 0;
    for (int i = 0; i < st->n && n < max; i++) {
        if (scope_fam >= 0 && ns_family_of(st, i) != scope_fam) continue;
        if (ci_contains(st->rec[i].name, q)) out[n++] = i;
    }
    return n;                     /* 0 is a VALID RESULT: the empty-search screen, not an error */
}
