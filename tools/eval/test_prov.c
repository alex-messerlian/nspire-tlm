#include <stdio.h>
#include <string.h>
int prov_unsourced(const char *doc, double *first);
int prov_call_unsourced(const char *doc, double *first);
int main(void) {
    struct { const char *doc; int want; const char *what; } T[] = {
    {"<q>A car travels 150 m in 12 s. Find the speed.</q><r>v=d/t | v:m/s"
     "<tool>eval<arg>150/12</arg></tool><res>12.5</res><a>12.5 m/s. Distance over time.<end>", 0,
     "states the computed result"},
    {"<q>A car travels 150 m in 12 s. Find the speed.</q><r>v=d/t | v:m/s"
     "<tool>eval<arg>150/12</arg></tool><res>12.5</res><a>About 13 m/s, distance over time.<end>", 0,
     "rounds the result to 2 sf -- legitimate"},
    {"<q>A car travels 150 m in 12 s. Find the speed.</q><r>v=d/t | v:m/s"
     "<tool>eval<arg>150/12</arg></tool><res>12.5</res><a>12.5 m/s, so it covers 900 m in a minute.<end>", 1,
     "900 was never computed -- UNSOURCED"},
    {"<q>Find the KE of a 0.145 kg ball at 40 m/s.</q><r>K=0.5*m*v^2"
     "<tool>eval<arg>0.5*0.145*40^2</arg></tool><res>116</res><a>116 J, from the 0.145 kg mass at 40 m/s.<end>", 0,
     "restates question numbers -- all sourced"},
    {"<q>A ball falls from 5 m.</q><r>U=m*g*h | g=9.81<a>The speed is 9.9 m/s.<end>", 1,
     "no tool call at all, number invented -- UNSOURCED"},
    {"<q>Why is momentum conserved?</q><r>explanation<a>"
     "Because no external force acts on the system.<end>", 0,
     "conceptual answer, no numbers"},
    };
    int fail = 0;
    for (unsigned i = 0; i < sizeof T/sizeof*T; i++) {
        double f = 0; int got = prov_unsourced(T[i].doc, &f);
        int ok = (got == T[i].want);
        if (!ok) fail++;
        printf("  %s want=%d got=%d%*s %s\n", ok?"ok ":"** ", T[i].want, got,
               got?12:12, got?"":"", T[i].what);
        if (got && ok) printf("        first unsourced: %g\n", f);
    }
    /* Invariant 2 on the PREMISES, not only the arithmetic. */
    struct { const char *doc; int want; const char *what; } C[] = {
    {"<q>A car travels 150 m in 12 s.</q><r>v=d/t | v:m/s<tool>eval<arg>150/12</tool>"
     "<res>12.5</res><a>12.5 m/s.<end>", 0, "call args come from the question"},
    {"<q>What is the speed of the car after 4.0 s?</q><r>C=epsilon_0*(A/d)"
     "<tool>eval<arg>(6.0)/(4.0)</tool><res>1.5</res><a>The intensity is 1.5.<end>", 1,
     "CONFABULATED: 6.0 appears nowhere in question or record"},
    {"<q>A 2 kg mass is 5 m up.</q><r>U=m*g*h | g=9.81<tool>eval<arg>2*9.81*5</tool>"
     "<res>98.1</res><a>98.1 J.<end>", 0, "constant 9.81 traces to the record"},
    {"<q>A 0.145 kg ball moves at 40 m/s.</q><r>K=0.5*m*v^2<tool>eval<arg>0.5*0.145*40^2</tool>"
     "<res>116</res><a>116 J.<end>", 0, "the 0.5 and the exponent trace to the record formula"},
    };
    for (unsigned i = 0; i < sizeof C/sizeof*C; i++) {
        double f = 0; int got = prov_call_unsourced(C[i].doc, &f);
        int ok = (got > 0) == (C[i].want > 0);
        if (!ok) fail++;
        printf("  %s call-args want%s got=%d  %s\n", ok?"ok ":"** ",
               C[i].want?">0":"=0", got, C[i].what);
    }
    printf("%s\n", fail ? "FAIL" : "all provenance checks passed");
    return fail != 0;
}
