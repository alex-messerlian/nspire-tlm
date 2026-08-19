/* fuzz.c -- TOOL_SPEC.md section 6.3 says "never crash". That is a claim, so it gets tested.
 *
 * Two generators. `garbage` throws structured noise at the wire parser. `mutate` builds a VALID
 * call and then corrupts it, which is what actually reaches the expression parser, the
 * differentiator, the polynomial extractor and the integrator -- the deep paths where a crash would
 * actually live. Deterministic PRNG, so any failure reproduces from the seed alone.
 */
#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned S = 1;
static unsigned rnd(void) { S = S * 1103515245u + 12345u; return (S >> 16) & 0x7FFF; }

static const char *EXPRS[] = {
    "1+1","2x","x^2","3x^2+2x+1","sin(x)","ln(x)","exp(x)","sqrt(x)","1/x","x^3-1",
    "9.8 m/s^2 * 3 s","5 km + 300 m","|x-2|","x!","2^3^2","-x^2","atan(x)/x","max(x,2)",
    "((x))","x/(x-1)","tan(x)^2","pi*x","e^x","0","1e300","1e-300","x^32","-0"
};
static const char *NAMES[] = {"eval","evalat","solve","diff","integ","conv","stat","nope",""};
static const char *ARGS2[] = {"x","y","mean","median","sd","var","n","m/s","min","0","1","-1","pi"};

#define NE ((int)(sizeof EXPRS / sizeof EXPRS[0]))
#define NN ((int)(sizeof NAMES / sizeof NAMES[0]))
#define NA ((int)(sizeof ARGS2 / sizeof ARGS2[0]))

static void garbage(char *buf, size_t cap) {
    static const char alpha[] = "0123456789+-*/^()|=,.xyz absincoqrtlgevmax<>/[]{}!\t ";
    static const char *frag[] = {"<tool>","<arg>","</tool>","eval","solve","diff","integ","stat"};
    size_t k = 0;
    int parts = 1 + rnd() % 12;
    for (int j = 0; j < parts && k < cap - 40; j++) {
        if (rnd() % 3 == 0) {
            const char *f = frag[rnd() % 8];
            size_t L = strlen(f);
            memcpy(buf + k, f, L); k += L;
        } else {
            int L = rnd() % 24;
            for (int m = 0; m < L && k < cap - 40; m++) buf[k++] = alpha[rnd() % (sizeof alpha - 1)];
        }
    }
    buf[k] = 0;
}

static void mutate(char *buf, size_t cap) {
    int na = 1 + rnd() % 4;
    size_t k = (size_t)snprintf(buf, cap, "<tool>%s", NAMES[rnd() % NN]);
    for (int i = 0; i < na && k < cap - 40; i++)
        k += (size_t)snprintf(buf + k, cap - k, "<arg>%s",
                              (rnd() % 2) ? EXPRS[rnd() % NE] : ARGS2[rnd() % NA]);
    snprintf(buf + k, cap - k, "</tool>");

    /* Corrupt it: a 45M model emits near-misses far more often than pure noise. */
    size_t L = strlen(buf);
    int muts = rnd() % 4;
    for (int i = 0; i < muts && L; i++) {
        size_t at = rnd() % L;
        switch (rnd() % 3) {
            case 0: buf[at] = "()|^*/+-=,.<>x0"[rnd() % 15]; break;
            case 1: memmove(buf + at, buf + at + 1, L - at); L--; break;
            case 2: buf[at] = (char)(0x20 + rnd() % 0x5F);   break;
        }
    }
}

int main(int argc, char **argv) {
    int N = argc > 1 ? atoi(argv[1]) : 200000;
    if (argc > 2) S = (unsigned)atoi(argv[2]);
    char buf[MAX_CALL_BYTES + 64], out[MAX_RESULT];
    int ok = 0, err = 0, tberr = 0;

    for (int i = 0; i < N; i++) {
        if (i % 4 == 0) garbage(buf, sizeof buf); else mutate(buf, sizeof buf);
        tb_status st = tool_call_text(buf, out, sizeof out);
        if (st == TB_ERR)      tberr++;
        else if (out[0] == '!') err++;
        else                   ok++;
    }
    printf("fuzz: %d inputs, no crash. ok=%d err=%d tb_err=%d\n", N, ok, err, tberr);
    return 0;
}
