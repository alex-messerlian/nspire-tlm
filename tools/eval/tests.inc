/* tests.inc -- the acceptance test bodies, shared verbatim by the host suite (test_eval.c) and the
 * device suite (device_main.c). Single source of truth: two copies of a golden list would drift,
 * and a drifted oracle is worse than no oracle. */
static void run_tests(void) {

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
    T("<tool>eval<arg>1e9</tool>",                     "1000000000");  /* ip == nd */
    T("<tool>eval<arg>2500000000</tool>",              "2500000000");
    T("<tool>eval<arg>1e300*1e300</tool>",             "!range");      /* overflow */

    /* ---- functions ---- */
    T("<tool>eval<arg>sqrt(16)</tool>",                "4");
    T("<tool>eval<arg>ln(e)</tool>",                   "1");
    T("<tool>eval<arg>log(1000)</tool>",               "3");       /* log is base 10 */
    T("<tool>eval<arg>sin(0)</tool>",                  "0");
    T("<tool>eval<arg>max(3,7)</tool>",                "7");

    /* ---- implicit multiplication binds tighter than / (physics unit notation) ---- */
    /* Regression: these all returned inverted denominators before 2026-08-20 and would have put
     * wrong units through the whole physics corpus. Found by design review, not by these tests. */
    /* AMBIGUOUS -- refused, not guessed. "100 m/10 s" wants (100 m)/(10 s) while "1/2 m v^2"
     * wants (1/2)*m*v^2; no precedence satisfies both, so we reject rather than silently pick one.
     * Same principle as solve() returning !nosol instead of an unreliable root. */
    T("<tool>eval<arg>100 m/10 s</tool>",              "!expr");
    T("<tool>eval<arg>12 V/0.25 A</tool>",             "!expr");
    T("<tool>eval<arg>20 N/4 kg</tool>",               "!expr");
    T("<tool>eval<arg>1/2 2 3</tool>",                 "!expr");
    /* parenthesised forms are unambiguous and must work */
    T("<tool>conv<arg>100 m/(10 s)<arg>m/s</tool>",    "10 m/s");
    T("<tool>conv<arg>12 V/(0.25 A)<arg>ohm</tool>",   "48 ohm");
    T("<tool>conv<arg>20 N/(4 kg)<arg>m/s^2</tool>",   "5 m/s^2");
    T("<tool>conv<arg>500 J/(25 s)<arg>W</tool>",      "20 W");
    T("<tool>conv<arg>60 km/(2 h)<arg>m/s</tool>",     "8.333333333 m/s");
    /* single-factor denominators stay unambiguous */
    T("<tool>conv<arg>9.8 m/s^2 * 3 s<arg>m/s</tool>", "29.4 m/s");
    T("<tool>conv<arg>100 km/h<arg>m/s</tool>",        "27.77777778 m/s");
    /* canonical form: numeric factors pulled left regardless of nesting */
    T("<tool>diff<arg>m*x^2<arg>x</tool>",             "2*m*x");
    T("<tool>diff<arg>a*b*x^2<arg>x</tool>",           "2*a*b*x");
    /* explicit operators keep strict left-to-right */
    T("<tool>evalat<arg>1/2*x<arg>x<arg>8</tool>",     "4");
    T("<tool>eval<arg>100/10/2</tool>",                "5");
    T("<tool>eval<arg>2*3 x</tool>",                   "!expr");   /* x unbound, but parses */

    /* ---- v1.2.0: units resolve ONLY in conv. Every identifier is a symbol elsewhere. ----
     * Pins the entire colliding set. Before this, evalat(m*g*h, m, 2) silently returned 7.2 kg*s
     * with g as GRAMS and h as HOURS -- a confident, well-formatted, physically meaningless number.
     * These are the symbols in nearly every intro-mechanics formula. */
    T("<tool>evalat<arg>m*g*h<arg>m<arg>2</tool>",     "!expr");
    T("<tool>evalat<arg>0.5*m*v^2<arg>v<arg>20</tool>","!expr");
    T("<tool>evalat<arg>v*t<arg>v<arg>10</tool>",      "!expr");
    T("<tool>eval<arg>9.8 m/s^2</tool>",               "!expr");
    T("<tool>eval<arg>g</tool>",                       "!expr");
    T("<tool>eval<arg>h</tool>",                       "!expr");
    T("<tool>eval<arg>t</tool>",                       "!expr");
    T("<tool>eval<arg>s</tool>",                       "!expr");
    T("<tool>eval<arg>T</tool>",                       "!expr");
    T("<tool>eval<arg>A</tool>",                       "!expr");
    T("<tool>eval<arg>V</tool>",                       "!expr");
    T("<tool>eval<arg>C</tool>",                       "!expr");
    T("<tool>eval<arg>F</tool>",                       "!expr");
    T("<tool>eval<arg>N</tool>",                       "!expr");
    T("<tool>eval<arg>K</tool>",                       "!expr");
    T("<tool>eval<arg>m</tool>",                       "!expr");
    /* constants are NOT units and must still resolve */
    T("<tool>eval<arg>pi</tool>",                      "3.141592654");
    T("<tool>evalat<arg>0.5*80*v^2<arg>v<arg>20</tool>","16000");

    /* ---- units (conv only) ---- */
    T("<tool>conv<arg>9.8 m/s^2 * 3 s<arg>m/s</tool>", "29.4 m/s");
    T("<tool>conv<arg>2 kg*(3 m/s^2)<arg>N</tool>",    "6 N");     /* coherent derived unit */
    T("<tool>conv<arg>12 V*(2 A)<arg>W</tool>",        "24 W");

    /* ---- v1.1: ambiguous dimension vectors are NOT collapsed (TOOL_SPEC 5.4) ---- */
    /* energy vs torque -- both m^2*kg/s^2, so neither name may be guessed */
    T("<tool>conv<arg>0.5*80 kg*(20 m/s)^2<arg>J</tool>", "16000 J");
    T("<tool>conv<arg>0.5*80 kg*(20 m/s)^2<arg>J</tool>", "16000 J");
    T("<tool>conv<arg>50 N*m<arg>N*m</tool>",          "50 N*m");
    /* frequency vs angular velocity -- both 1/s */
    T("<tool>conv<arg>1/(0.5 s)<arg>Hz</tool>",        "2 Hz");
    T("<tool>conv<arg>1/(0.5 s)<arg>Hz</tool>",        "2 Hz");
    T("<tool>conv<arg>2*pi*3/(1 s)<arg>rad/s</tool>",  "18.84955592 rad/s");
    /* Pa is deliberately NOT blocked: pressure, stress and modulus are all correctly written Pa */
    T("<tool>conv<arg>100 N/(2 m^2)<arg>Pa</tool>",    "50 Pa");
    T("<tool>conv<arg>3 kg*m/s<arg>kg*m/s</tool>",     "3 kg*m/s"); /* no derived name -> base */
    T("<tool>conv<arg>25 degC<arg>K</tool>",           "298.15 K"); /* affine, conv only */
    T("<tool>conv<arg>98.6 degF<arg>degC</tool>",      "37 degC");
    T("<tool>conv<arg>273.15 K<arg>degC</tool>",       "0 degC");
    T("<tool>conv<arg>25 degC + 1<arg>K</tool>",       "!units");   /* affine in a compound expr */
    T("<tool>conv<arg>5 km + 300 m<arg>m</tool>",      "5300 m");
    T("<tool>conv<arg>1 m + 1 s<arg>m</tool>",         "!units");
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
    T("<tool>solve<arg>-x^2+4=0<arg>x</tool>",         "x=-2, x=2");   /* negation in extraction */
    T("<tool>solve<arg>x/2+1=0<arg>x</tool>",          "x=-2");        /* division in extraction */
    T("<tool>solve<arg>x/x=1<arg>x</tool>",            "!nosol");      /* var in a denominator */
    T("<tool>solve<arg>x-pi=0<arg>x</tool>",           "x=3.141592654");
    T("<tool>solve<arg>2x-6<arg>x</tool>",             "x=3");         /* bare expr, implicit =0 */

    /* ---- E1: linear literal isolation ---- */
    T("<tool>solve<arg>x+y=0<arg>x</tool>",            "x=-y");
    T("<tool>solve<arg>F=m*a<arg>a</tool>",            "a=F/m");
    T("<tool>solve<arg>F=m*a<arg>m</tool>",            "m=F/a");
    T("<tool>solve<arg>V=I*R<arg>R</tool>",            "R=V/I");
    T("<tool>solve<arg>P=2*l+2*w<arg>l</tool>",        "l=(P-2*w)/2");
    T("<tool>solve<arg>v=v0+a*t<arg>t</tool>",         "t=(v-v0)/a");
    T("<tool>solve<arg>PE=m*g*h<arg>h</tool>",         "h=PE/(m*g)");

    /* ---- E2: denominator clearing ---- */
    T("<tool>solve<arg>v=d/t<arg>t</tool>",            "t=d/v");
    T("<tool>solve<arg>P=W/t<arg>t</tool>",            "t=W/P");
    T("<tool>solve<arg>rho=m/V<arg>V</tool>",          "V=m/rho");
    T("<tool>solve<arg>P=F/A<arg>A</tool>",            "A=F/P");

    /* ---- E3r: root extraction ---- */
    T("<tool>solve<arg>A=pi*r^2<arg>r</tool>",         "r=+-sqrt(A/pi)");

    /* ---- still refused, by decision ---- */
    T("<tool>solve<arg>d=v0*t+0.5*a*t^2<arg>t</tool>", "!nosol");   /* full quadratic literal = E3 */
    T("<tool>solve<arg>sin(x)=y<arg>x</tool>",         "!nosol");   /* not polynomial in x */
    T("<tool>solve<arg>x^3=y<arg>x</tool>",            "!nosol");   /* degree 3 */
    T("<tool>solve<arg>sqrt(4)*x-8=0<arg>x</tool>",    "x=4");         /* fn of a constant */

    /* ---- diff ---- */
    T("<tool>diff<arg>x^2<arg>x</tool>",               "2*x");
    T("<tool>diff<arg>3x^2+2x+1<arg>x</tool>",         "6*x+2");
    T("<tool>diff<arg>sin(x)<arg>x</tool>",            "cos(x)");
    T("<tool>diff<arg>ln(x)<arg>x</tool>",             "1/x");
    T("<tool>diff<arg>exp(x)<arg>x</tool>",            "exp(x)");
    T("<tool>diff<arg>5<arg>x</tool>",                 "0");
    T("<tool>diff<arg>y<arg>x</tool>",                 "0");
    T("<tool>diff<arg>abs(x)<arg>x</tool>",            "!nosol");
    T("<tool>diff<arg>cos(x)<arg>x</tool>",            "-sin(x)");
    T("<tool>diff<arg>tan(x)<arg>x</tool>",            "1/cos(x)^2");
    T("<tool>diff<arg>sqrt(x)<arg>x</tool>",           "1/(2*sqrt(x))");
    T("<tool>diff<arg>log(x)<arg>x</tool>",            "1/(x*ln(10))");
    T("<tool>diff<arg>atan(x)<arg>x</tool>",           "1/(1+x^2)");
    T("<tool>diff<arg>asin(x)<arg>x</tool>",           "1/sqrt(1-x^2)");
    T("<tool>diff<arg>acos(x)<arg>x</tool>",           "-1/sqrt(1-x^2)");
    T("<tool>diff<arg>sinh(x)<arg>x</tool>",           "cosh(x)");
    T("<tool>diff<arg>tanh(x)<arg>x</tool>",           "1/cosh(x)^2");
    T("<tool>diff<arg>2^x<arg>x</tool>",               "2^x*ln(2)");   /* general u^v rule */
    T("<tool>diff<arg>x-x<arg>x</tool>",               "0");

    /* ---- v1.1: descending-degree sum ordering (TOOL_SPEC 5.3) ---- */
    T("<tool>diff<arg>1+2x+3x^2<arg>x</tool>",         "6*x+2");       /* ascending input */
    T("<tool>diff<arg>x^2+x^3<arg>x</tool>",           "3*x^2+2*x");
    T("<tool>diff<arg>x+x^4<arg>x</tool>",             "4*x^3+1");
    T("<tool>diff<arg>5x-x^2<arg>x</tool>",            "-2*x+5");
    T("<tool>diff<arg>-x^3<arg>x</tool>",              "-3*x^2");
    T("<tool>eval<arg>3-(-2)</tool>",                  "5");

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
}
