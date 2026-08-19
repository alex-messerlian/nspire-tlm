/* fmt.c -- deterministic number formatting.
 *
 * TOOL_SPEC.md section 5.1 requires byte-identical output on host and device. The host is Apple libc,
 * the device is newlib. printf("%g") and printf("%.9e") are NOT guaranteed to agree in the last digit
 * between two libc implementations, and a one-digit disagreement silently corrupts every training
 * association built from that number.
 *
 * So: reduce to exactly 10 significant digits as a 64-bit INTEGER, format that integer (exact and
 * identical everywhere), and place the decimal point by hand.
 */
#include "eval.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

const char *err_code(err_t e) {
    switch (e) {
        case E_NONE:   return "";
        case E_PARSE:  return "!parse";
        case E_NAME:   return "!name";
        case E_ARITY:  return "!arity";
        case E_EXPR:   return "!expr";
        case E_DOMAIN: return "!domain";
        case E_UNITS:  return "!units";
        case E_NOSOL:  return "!nosol";
        case E_RANGE:  return "!range";
    }
    return "!range";
}

#define SIG_DIGITS 10
#define EXP_LIMIT  300

/* Write the decimal digits of a non-negative int64 into buf. Exact on every conformant C impl. */
static int digits_of(long long n, char *buf) {
    char tmp[24];
    int k = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = 0; return 1; }
    while (n > 0) { tmp[k++] = (char)('0' + (int)(n % 10)); n /= 10; }
    for (int i = 0; i < k; i++) buf[i] = tmp[k - 1 - i];
    buf[k] = 0;
    return k;
}

err_t fmt_number(double x, char *out, size_t sz) {
    if (sz < 32) return E_RANGE;
    if (isnan(x)) return E_DOMAIN;
    if (isinf(x)) return E_RANGE;

    if (x == 0.0) { out[0] = '0'; out[1] = 0; return E_NONE; }   /* also catches -0.0 */

    int neg = (x < 0.0);
    double a = fabs(x);

    /* Decimal exponent. log10 can land one off at powers of ten across libms, so we correct by
     * checking the scaled value's range rather than trusting it. */
    int e10 = (int)floor(log10(a));
    if (e10 > EXP_LIMIT || e10 < -EXP_LIMIT) return E_RANGE;

    double scaled = a / pow(10.0, (double)(e10 - (SIG_DIGITS - 1)));
    if (scaled >= 1e10) { e10++; scaled = a / pow(10.0, (double)(e10 - (SIG_DIGITS - 1))); }
    if (scaled <  1e9)  { e10--; scaled = a / pow(10.0, (double)(e10 - (SIG_DIGITS - 1))); }

    long long n = (long long)(scaled + 0.5);
    if (n >= 10000000000LL) { n /= 10; e10++; }      /* rounding carried into a new decade */
    if (n <  1000000000LL)  return E_RANGE;          /* should be unreachable */

    char d[24];
    int nd = digits_of(n, d);                        /* exactly 10 digits */

    char body[64];
    int p = 0;
    if (neg) body[p++] = '-';

    if (e10 >= -4 && e10 < 10) {
        /* Fixed notation. Decimal point sits after (e10 + 1) significant digits. */
        int ip = e10 + 1;
        if (ip <= 0) {
            body[p++] = '0'; body[p++] = '.';
            for (int i = 0; i < -ip; i++) body[p++] = '0';
            for (int i = 0; i < nd; i++) body[p++] = d[i];
        } else if (ip >= nd) {
            for (int i = 0; i < nd; i++) body[p++] = d[i];
            for (int i = 0; i < ip - nd; i++) body[p++] = '0';
        } else {
            for (int i = 0; i < ip; i++) body[p++] = d[i];
            body[p++] = '.';
            for (int i = ip; i < nd; i++) body[p++] = d[i];
        }
        body[p] = 0;
        /* Strip trailing zeros, then a bare trailing '.' */
        if (strchr(body, '.')) {
            while (p > 0 && body[p - 1] == '0') body[--p] = 0;
            if (p > 0 && body[p - 1] == '.') body[--p] = 0;
        }
    } else {
        /* Scientific: d.ddddddddde±dd */
        body[p++] = d[0];
        int frac = p;
        body[p++] = '.';
        for (int i = 1; i < nd; i++) body[p++] = d[i];
        body[p] = 0;
        while (p > frac + 1 && body[p - 1] == '0') body[--p] = 0;
        if (p > 0 && body[p - 1] == '.') body[--p] = 0;
        body[p++] = 'e';
        body[p++] = (e10 < 0) ? '-' : '+';
        int ae = e10 < 0 ? -e10 : e10;
        if (ae >= 100) { body[p++] = (char)('0' + ae / 100); ae %= 100; }
        body[p++] = (char)('0' + ae / 10);
        body[p++] = (char)('0' + ae % 10);
        body[p] = 0;
    }

    if (strlen(body) + 1 > sz) return E_RANGE;
    strcpy(out, body);
    return E_NONE;
}

err_t fmt_quant(quant_t q, char *out, size_t sz) {
    char num[64], un[64];
    err_t e = fmt_number(q.v, num, sizeof num);
    if (e != E_NONE) return e;
    units_render(q.d, un, sizeof un);
    if (un[0] == 0) {
        if (strlen(num) + 1 > sz) return E_RANGE;
        strcpy(out, num);
    } else {
        if (strlen(num) + 1 + strlen(un) + 1 > sz) return E_RANGE;
        snprintf(out, sz, "%s %s", num, un);
    }
    return E_NONE;
}
