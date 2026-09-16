#!/usr/bin/env python3
"""Linearise MathML and normalise unicode into the notation the SHIPPED STORE already uses.

WHY THIS EXISTS. `corpus/extract_knowledge.py` deleted every <m:math> block outright, on the
reasoning that "MathML renders as noise in prose". Measured, that deleted the CONTENT along with the
markup: 377 math blocks sit inside <definition> elements alone, and stripping them amputated both
terms and meanings --

    <term><m:math><m:mi>@</m:mi></m:math>-particle</term>  ->  "-particle"     (@ = literal alpha)
    "SI unit for current; <m:math>..1 A = 1 C/s..</m:math>" ->  "SI unit for current;"

185 of 1,589 definitions (11.6%) are under 45 chars and this is the main reason.

WHY THIS NOTATION AND NOT UNICODE. `corpus/store_clean.json` -- the file that ships to the device --
holds ZERO literal non-ASCII characters and spells every Greek letter out: `theta_1`, `omega`,
`Delta_U`, `lambda`. The compute tier and the knowledge tier appear in the SAME context window, so a
definition carrying a literal Greek theta against a record written `theta` is train/serve skew in the
model's own alphabet. It is also cheaper: the project log records the tokenizer byte-falls-back on a
literal theta (2 tokens) and emits <unk> for a literal radical sign.

Every mapping below is applied to the 43 distinct non-ASCII characters measured across every
<definition> in the three OpenStax physics books. `render()` reports what it could not parse rather
than silently dropping it -- "cannot render" and "rendered clean" must not share an exit status.
"""
import re
import xml.etree.ElementTree as ET

# Greek -> the store's own spelling. Capitals keep their case (`Delta`, `Omega`), matching
# store_clean.json, where `Delta` appears 69 times and no literal Greek character appears at all.
GREEK = {
    "\u03b1": "alpha", "\u03b2": "beta", "\u03b3": "gamma", "\u03b4": "delta",
    "\u03b5": "epsilon", "\u03f5": "epsilon", "\u03b6": "zeta", "\u03b7": "eta",
    "\u03b8": "theta", "\u03d1": "theta", "\u03b9": "iota", "\u03ba": "kappa",
    "\u03bb": "lambda", "\u03bc": "mu", "\u00b5": "mu", "\u03bd": "nu", "\u03be": "xi",
    "\u03bf": "omicron", "\u03c0": "pi", "\u03c1": "rho", "\u03c2": "sigma",
    "\u03c3": "sigma", "\u03c4": "tau", "\u03c5": "upsilon", "\u03c6": "phi",
    "\u03d5": "phi", "\u03c7": "chi", "\u03c8": "psi", "\u03c9": "omega",
    "\u0391": "Alpha", "\u0392": "Beta", "\u0393": "Gamma", "\u0394": "Delta",
    "\u0395": "Epsilon", "\u0396": "Zeta", "\u0397": "Eta", "\u0398": "Theta",
    "\u0399": "Iota", "\u039a": "Kappa", "\u039b": "Lambda", "\u039c": "Mu",
    "\u039d": "Nu", "\u039e": "Xi", "\u039f": "Omicron", "\u03a0": "Pi",
    "\u03a1": "Rho", "\u03a3": "Sigma", "\u03a4": "Tau", "\u03a5": "Upsilon",
    "\u03a6": "Phi", "\u03a7": "Chi", "\u03a8": "Psi", "\u03a9": "Omega",
}

# Everything else the measurement found. Operators become their ASCII equivalent, typography the
# closest ASCII. Three are genuine source typos: U+04E7 is a Cyrillic o standing in for the umlaut in
# Schrodinger, U+00BA a masculine ordinal standing in for a degree sign, U+0097 a stray control byte.
PUNCT = {
    "\u2019": "'", "\u2018": "'", "\u201c": '"', "\u201d": '"', "\u2032": "'",
    "\u2212": "-", "\u2013": "-", "\u2014": ", ", "\u2015": "-", "\u00ad": "",
    "\u00d7": "*", "\u22c5": "*", "\u00b7": "*", "\u2219": "*", "\u2217": "*",
    "\u00f7": "/", "\u2044": "/", "\u00b1": "+/-", "\u2213": "-/+",
    "\u2260": "!=", "\u2264": "<=", "\u2265": ">=", "\u2248": "~=", "\u2261": "=",
    "\u221d": " proportional to ", "\u2192": " -> ", "\u2190": " <- ",
    "\u221a": "sqrt", "\u221e": " infinity ", "\u2211": " sum ", "\u220f": " product ",
    "\u222b": " integral ", "\u2202": "d", "\u2207": " grad ", "\u00b0": " degrees ",
    "\u00ba": " degrees ", "\u00a0": " ", "\u2009": " ", "\u200a": " ", "\u2008": " ",
    "\u2007": " ", "\u2002": " ", "\u2003": " ", "\u200b": "", "\u2026": "...",
    "\u00af": "", "\u0097": " ", "\u04e7": "o", "\u00e8": "e", "\u00e9": "e",
    "\u00f6": "o", "\u00fc": "u", "\u00e4": "a", "\u00e5": "a", "\u00e7": "c",
    "\u00ee": "i", "\u00ef": "i", "\u00e2": "a", "\u00ea": "e", "\u00f4": "o",
    "\u00bd": "1/2", "\u00bc": "1/4", "\u00be": "3/4",
    "\u2080": "_0", "\u2081": "_1", "\u2082": "_2", "\u2083": "_3",
    "\u00b2": "^2", "\u00b3": "^3", "\u00b9": "^1", "\u2070": "^0",
    "\u2039": "<", "\u203a": ">", "\u00ab": '"', "\u00bb": '"',
    # Vector, calculus and comparison notation, measured across all three books (the earlier
    # inventory of 43 covered <definition> blocks only; section prose uses far more).
    "\u21d2": ' implies ',
    "\u21d0": ' implied by ',
    "\u21d4": ' iff ',
    "\u2194": ' <-> ',
    "\u210f": 'hbar',
    "\u2113": 'l',
    "\u22a5": ' perpendicular to ',
    "\u2225": ' parallel to ',
    "\u01c1": ' parallel to ',
    "\u222e": ' integral ',
    "\u222c": ' integral ',
    "\u222d": ' integral ',
    "\u22ef": '...',
    "\u22ee": '...',
    "\u22f1": '...',
    "\u2218": ' degrees ',
    "\u2022": '*',
    "\u2191": ' up ',
    "\u2193": ' down ',
    "\u2329": '<',
    "\u232a": '>',
    "\u27e8": '<',
    "\u27e9": '>',
    "\u2223": '|',
    "\u226b": ' >> ',
    "\u226a": ' << ',
    "\u00c5": 'Angstrom',
    "\u2245": '~=',
    "\u2243": '~=',
    "\u223c": '~',
    "\u02dc": '~',
    "\u03d2": 'Upsilon',
    "\u2033": "''",
    "\u2234": ' therefore ',
    "\u2235": ' because ',
    "\u2205": 'empty set',
    "\u2208": ' in ',
    "\u2209": ' not in ',
    "\u2286": ' subset of ',
    "\u00ac": 'not ',
    "\u2227": ' and ',
    "\u2228": ' or ',
    "\u2295": '+',
    "\u2297": '*',
    "\u221f": 'angle',
    "\u2220": 'angle ',
    "\u212b": 'Angstrom',
    "\u2103": ' degrees C',
    "\u2109": ' degrees F',
    "\u00b6": '',
    "\u00a7": 'section ',
    "\u2020": '',
    "\u2021": '',
    "\u02da": ' degrees ',
}


def ascii_fold(s):
    """Map every character the OpenStax source uses onto the store's ASCII alphabet."""
    out = []
    for ch in s:
        if ord(ch) < 128:
            out.append(ch)
        elif ch in GREEK:
            out.append(GREEK[ch])
        elif ch in PUNCT:
            out.append(PUNCT[ch])
        else:
            out.append(" ")          # counted by unmapped(), never silently assumed empty
    return "".join(out)


# A capital Delta is a PREFIX OPERATOR meaning "change in", and store_clean.json spells every one of
# its twenty occurrences with an underscore -- Delta_t, Delta_x, Delta_E_int. Joining it to the
# following symbol keeps the knowledge tier in the compute tier's alphabet.
#
# It is scoped to Delta DELIBERATELY. The same rule over all Greek letters changes 9.7% of physics
# math blocks and is wrong on most of them, because every other Greek letter here is a multiplicand
# or a unit prefix, not an index: it turns "pi*r^2" into "pi_r^2", "rho*L/A" into "rho_L/A" and
# "1.00 muA" into "1.00 mu_A". The OpenStax MathML carries no invisible-times operator (measured: 0
# occurrences of U+2062 in 44,669 physics blocks), so nothing in the source distinguishes a product
# from an index, and a heuristic that guesses would be the misdispatch class the project log records.
_DELTA = re.compile(r"[\u0394\u2206](?=[A-Za-z\u0391-\u03c9])")


def fold(s):
    """ascii_fold, plus the Delta-prefix join. This is what callers should use."""
    return ascii_fold(_DELTA.sub("\u0394_", s))


def unmapped(s):
    """The characters ascii_fold would drop. Reported, so a new source cannot widen the hole."""
    return {ch for ch in s if ord(ch) >= 128 and ch not in GREEK and ch not in PUNCT}


# Overscripts that DECORATE rather than index: a vector arrow, hat, bar, tilde or dot. Treating
# them as a subscript rendered momentum-as-a-vector "p_ -> ".
DECOR = {"-", "_", "^", "~", ".", "..", "->", "<-", "<->", "", " ",
         "\u2192", "\u2190", "\u2194", "\u00af", "\u02c6", "\u02dc", "\u02d9",
         "\u0302", "\u0304", "\u0307", "\u20d7", "\u2015", "\u203e"}

_NS = "http://www.w3.org/1998/Math/MathML"


def _tag(e):
    return e.tag.split("}")[-1] if "}" in e.tag else e.tag


def _kids(e, sep=""):
    # MathML inter-element whitespace is insignificant (MathML 3 sec 2.1.7). Keeping element
    # tails split every multi-token number and unit -- "2 . 00 * 10^9", "50.0 k m^2". Genuine
    # spaces arrive as <mtext> content or <mspace>, both of which are preserved.
    return sep.join(_node(k) for k in e)


def _wrap(s):
    """Parenthesise a sub-expression unless it is already a single atom."""
    s = s.strip()
    if len(s) <= 1 or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*|\d+(\.\d+)?|\([^()]*\)", s):
        return s
    return "(" + s + ")"


def _node(e):
    t = _tag(e)
    txt = e.text or ""
    if t in ("mi", "mn", "mo", "mtext", "ms"):
        return txt + _kids(e)
    if t in ("math", "mrow", "mstyle", "mpadded", "semantics", "mphantom", "merror"):
        return txt + _kids(e)
    if t == "mspace":
        return " "
    if t in ("msub", "msup", "munder", "mover"):
        ch = list(e)
        if len(ch) != 2:
            return txt + _kids(e)
        base, sub = _node(ch[0]).strip(), _node(ch[1]).strip()
        if t == "msub":
            return base + "_" + _wrap(sub)
        if t == "msup":
            return base + "^" + _wrap(sub)
        # an overbar, hat or underbrace is decoration; the base carries the meaning
        if not sub or sub in DECOR:
            return base
        return base + "_" + _wrap(sub)
    if t in ("msubsup", "munderover"):
        ch = list(e)
        if len(ch) != 3:
            return txt + _kids(e)
        return (_node(ch[0]).strip() + "_" + _wrap(_node(ch[1]).strip())
                + "^" + _wrap(_node(ch[2]).strip()))
    if t == "mfrac":
        ch = list(e)
        if len(ch) != 2:
            return txt + _kids(e)
        return _wrap(_node(ch[0])) + "/" + _wrap(_node(ch[1]))
    if t == "msqrt":
        return "sqrt(" + _kids(e).strip() + ")"
    if t == "mroot":
        ch = list(e)
        if len(ch) != 2:
            return txt + _kids(e)
        return _wrap(_node(ch[0])) + "^(1/" + _node(ch[1]).strip() + ")"
    if t == "mfenced":
        return "(" + _kids(e, ", ").strip() + ")"
    if t in ("mtable", "mtr"):
        return _kids(e, " ")
    return txt + _kids(e)


def render(block):
    """MathML source -> one linear expression.

    Returns (text, ok). ok=False means the block did not parse and the caller got the tag-stripped
    fallback, which is the OLD behaviour: it must be counted, never assumed rare.
    """
    try:
        root = ET.fromstring('<wrap xmlns:m="%s">%s</wrap>' % (_NS, block))
    except ET.ParseError:
        return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", block)).strip(), False
    s = "".join(_node(k) for k in root)
    return re.sub(r"\s+", " ", s).strip(), True
