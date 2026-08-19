/* test_eval.c -- acceptance tests for Backend 1.
 *
 * These are the contract. If the device build does not reproduce this file's output byte for byte,
 * the port is wrong and no training data generated against it is valid.
 */
#include "eval.h"
#include <stdio.h>
#include <string.h>

static int pass = 0, fail_n = 0;

static void T(const char *call, const char *want) {
    char got[MAX_RESULT];
    tb_status st = tool_call_text(call, got, sizeof got);
    if (st == TB_ERR) strcpy(got, "<TB_ERR>");
    if (strcmp(got, want) == 0) { pass++; }
    else { fail_n++; printf("FAIL  %-52s want=%-22s got=%s\n", call, want, got); }
}

int main(void) {
    /* ---- arithmetic and precedence ---- */
    T("<tool>eval<arg>1+1</tool>",                     "2");
    T("<tool>eval<arg>2+3*4</tool>",                   "14");
    T("<tool>eval<arg>(2+3)*4</tool>",                 "20");
    T("<tool>eval<arg>2^3^2</tool>",                   "512");     /* right associative */
    T("<tool>eval<arg>-3^2</tool>",                    "-9");      /* unary binds looser than ^ */
    T("<tool>eval<arg>2^-2</tool>",                    "0.25");
    T("<tool>eval<arg>7/2</tool>",                     "3.5");
    T("<tool>eval<arg>10-2-3</tool>",                  "5");       /* left associative */
    T("<tool>eval<arg>|3-7|</tool>",                   "4");
    T("<tool>eval<arg>2|3-7|</tool>",                  "8");      /* implicit mult onto a bar group */
    T("<tool>eval<arg>|3-7|+1</tool>",                 "5");
    T("<tool>eval<arg>5!</tool>",                      "120");

    /* ---- implicit multiplication ---- */
    T("<tool>evalat<arg>2x<arg>x<arg>5</tool>",        "10");
    T("<tool>evalat<arg>3(x+1)<arg>x<arg>2</arg></tool>", "!parse");   /* stray close tag */
    T("<tool>evalat<arg>3(x+1)<arg>x<arg>2</tool>",    "9");
    T("<tool>eval<arg>1.5e3</tool>",                   "1500");    /* exponent literal */
    T("<tool>eval<arg>2e</tool>",                      "5.436563657");  /* 2 * Euler's e */

    /* ---- deterministic formatting (TOOL_SPEC 5.1) ---- */
    T("<tool>eval<arg>1/3</tool>",                     "0.3333333333");
    T("<tool>eval<arg>2/3</tool>",                     "0.6666666667");
    T("<tool>eval<arg>1e10</tool>",                    "1e+10");
    T("<tool>eval<arg>0.0001</tool>",                  "0.0001");
    T("<tool>eval<arg>0.00001</tool>",                 "1e-05");
    T("<tool>eval<arg>0-0</tool>",                     "0");       /* never "-0" */
    T("<tool>eval<arg>pi</tool>",                      "3.141592654");

    /* ---- functions ---- */
    T("<tool>eval<arg>sqrt(16)</tool>",                "4");
    T("<tool>eval<arg>ln(e)</tool>",                   "1");
    T("<tool>eval<arg>log(1000)</tool>",               "3");       /* log is base 10 */
    T("<tool>eval<arg>sin(0)</tool>",                  "0");
    T("<tool>eval<arg>max(3,7)</tool>",                "7");

    /* ---- units ---- */
    T("<tool>eval<arg>9.8 m/s^2 * 3 s</tool>",         "29.4 m/s");
    T("<tool>eval<arg>2 kg * 3 m/s^2</tool>",          "6 N");     /* coherent derived unit */
    T("<tool>eval<arg>0.5*80 kg*(20 m/s)^2</tool>",    "16000 J");
    T("<tool>eval<arg>12 V*2 A</tool>",                "24 W");
    T("<tool>eval<arg>3 kg*m/s</tool>",                "3 m*kg/s"); /* no derived name -> base */
    T("<tool>conv<arg>25 degC<arg>K</tool>",           "298.15 K"); /* affine, conv only */
    T("<tool>conv<arg>98.6 degF<arg>degC</tool>",      "37 degC");
    T("<tool>conv<arg>273.15 K<arg>degC</tool>",       "0 degC");
    T("<tool>eval<arg>25 degC + 1</tool>",             "!units");   /* affine in a compound expr */
    T("<tool>eval<arg>5 km + 300 m</tool>",            "5300 m");
    T("<tool>eval<arg>1 m + 1 s</tool>",               "!units");
    T("<tool>conv<arg>1 h<arg>min</tool>",             "60 min");
    T("<tool>conv<arg>100 km/h<arg>m/s</tool>",        "27.77777778 m/s");
    T("<tool>conv<arg>1 kg<arg>V</tool>",              "!units");

    /* ---- solve ---- */
    T("<tool>solve<arg>2x+6=0<arg>x</tool>",           "x=-3");
    T("<tool>solve<arg>x^2-4=0<arg>x</tool>",          "x=-2, x=2");         /* ascending */
    T("<tool>solve<arg>2x^2+3x-5=0<arg>x</tool>",      "x=-2.5, x=1");
    T("<tool>solve<arg>x^2-2x+1=0<arg>x</tool>",       "x=1");               /* double root */
    T("<tool>solve<arg>x^2+1=0<arg>x</tool>",          "x=0+1i, x=0-1i");
    T("<tool>solve<arg>x=x<arg>x</tool>",              "all");
    T("<tool>solve<arg>x+1=x<arg>x</tool>",            "none");
    T("<tool>solve<arg>x^3-1=0<arg>x</tool>",          "!nosol");            /* degree > 2 */
    T("<tool>solve<arg>sin(x)=0<arg>x</tool>",         "!nosol");

    /* ---- diff ---- */
    T("<tool>diff<arg>x^2<arg>x</tool>",               "2*x");
    T("<tool>diff<arg>3x^2+2x+1<arg>x</tool>",         "6*x+2");
    T("<tool>diff<arg>sin(x)<arg>x</tool>",            "cos(x)");
    T("<tool>diff<arg>ln(x)<arg>x</tool>",             "1/x");
    T("<tool>diff<arg>exp(x)<arg>x</tool>",            "exp(x)");
    T("<tool>diff<arg>5<arg>x</tool>",                 "0");
    T("<tool>diff<arg>y<arg>x</tool>",                 "0");
    T("<tool>diff<arg>abs(x)<arg>x</tool>",            "!nosol");

    /* ---- integ ---- */
    T("<tool>integ<arg>x<arg>x<arg>0<arg>1</tool>",    "0.5");
    T("<tool>integ<arg>x^2<arg>x<arg>0<arg>3</tool>",  "9");
    T("<tool>integ<arg>sin(x)<arg>x<arg>0<arg>pi</tool>", "2");
    T("<tool>integ<arg>1<arg>x<arg>2<arg>0</tool>",    "-2");     /* reversed limits */

    /* ---- stat ---- */
    T("<tool>stat<arg>mean<arg>1,2,3,4</tool>",        "2.5");
    T("<tool>stat<arg>median<arg>1,2,3,4</tool>",      "2.5");
    T("<tool>stat<arg>median<arg>3,1,2</tool>",        "2");
    T("<tool>stat<arg>sd<arg>2,4,4,4,5,5,7,9</tool>",  "2.138089935");  /* SAMPLE sd, n-1 */
    T("<tool>stat<arg>n<arg>1 2 3</tool>",             "3");
    T("<tool>stat<arg>mode<arg>1,2,3</tool>",          "!name");

    /* ---- errors, the closed set (TOOL_SPEC 6.1) ---- */
    T("<tool>eval<arg>1/0</tool>",                     "!domain");
    T("<tool>eval<arg>ln(0)</tool>",                   "!domain");
    T("<tool>eval<arg>sqrt(-1)</tool>",                "!domain");
    T("<tool>eval<arg>asin(2)</tool>",                 "!domain");
    T("<tool>eval<arg>2+</tool>",                      "!parse");
    T("<tool>eval<arg>((1)</tool>",                    "!parse");
    T("<tool>eval<arg>zzz</tool>",                     "!expr");
    T("<tool>nope<arg>1</tool>",                       "!name");
    T("<tool>eval<arg>1<arg>2</tool>",                 "!arity");
    T("<tool>solve<arg>x=0</tool>",                    "!arity");
    T("garbage",                                       "!parse");
    T("<tool>eval<arg>1+1",                            "!parse");    /* unterminated */

    /* ---- never crash: adversarial input a 45M model will eventually emit ---- */
    T("<tool>eval<arg>((((((((((((((((((((1))))))))))))))))))))</tool>", "1");
    T("<tool>eval<arg>1+++++++++1</tool>",             "2");
    T("<tool>eval<arg></tool>",                        "!parse");
    T("<tool><arg>1</tool>",                           "!parse");

    printf("\n%d passed, %d failed\n", pass, fail_n);
    return fail_n ? 1 : 0;
}
