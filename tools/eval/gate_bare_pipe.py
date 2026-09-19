#!/usr/bin/env python3
"""Fails any consumer that splits a record span on a bare pipe instead of corpus/recfmt.

The property is "parses a record with the wrong separator", and the predicate has to be narrower
than `"|" in source`: a formula may contain a pipe, a markdown table is full of them, and a regex
alternation is not a record parse. So this looks for a split on a bare "|" applied to something
named like a record span, and exempts a file only when it actually imports recfmt -- an AST import
node, never the substring, which is how test_genloop's exemption was defeated by a local function
named genloop().
"""
import ast, pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
# THE PREDICATE IS THE SPLIT ITSELF, NOT A NEARBY NAME. The first version required a variable
# called record/span/rec within 40 characters, and missed all three retrieval harnesses, which do
# `m.group(1).split("|")[0]` on a record span pulled out by regex -- no such name in sight. Keying
# on a name is a proxy for "this value is a record span", and the value's provenance is not in its
# spelling. So every bare-pipe split is flagged and each is either routed through recfmt or listed
# below with the reason it is not a record.
# AND THE SPELLING OF THE CALL IS NOT THE PROPERTY EITHER. The first regex required the close paren
# immediately after the quote, so `.split("|", 1)` -- a maxsplit, which truncates a formula exactly
# as badly -- went straight through, and I wrote one into gate_explain_variants.py while holding
# this file open. A maxsplit and an rsplit are now both flagged; an rsplit that counts from the END
# is sound (it is what shapecheck.c does) but it is allowlisted with that reason rather than
# pattern-matched, because "counts from the end" is not something a regex can see.
BARE = re.compile(r'\.r?split\(\s*["\']\|["\']\s*(?:,\s*\d+\s*)?\)')
SKIP = {"gate_bare_pipe.py", "recfmt.py"}
ALLOW = {
    # file -> why this split is not a record span
    "tools/eval/gate_d3_legitimacy.py": "parses a markdown table row in docs/, not a record",
    "tools/eval/grade.py": "rsplit counts the 4 separators from the END, so a formula's internal "
                           "pipes stay in field 0 -- sound, and the comment there says so",
    "tools/eval/gate_format_parity.py": "rsplit counts the 4 separators from the END of the span, "
                                        "so a formula's internal pipes stay in field 0 -- the same "
                                        "approach src/store/shapecheck.c takes, and sound",
}


def imports_recfmt(tree):
    for n in ast.walk(tree):
        if isinstance(n, ast.ImportFrom) and n.module and "recfmt" in n.module:
            return True
        if isinstance(n, ast.Import) and any("recfmt" in a.name for a in n.names):
            return True
    return False


def offenders():
    bad = []
    for p in sorted(ROOT.glob("tools/eval/*.py")) + sorted(ROOT.glob("corpus/*.py")) + sorted(ROOT.glob("train/*.py")):
        rel = str(p.relative_to(ROOT))
        if p.name in SKIP or rel in ALLOW:
            continue
        src = p.read_text()
        hits = BARE.findall(src)
        if not hits:
            continue
        try:
            if imports_recfmt(ast.parse(src)):
                continue
        except SyntaxError:
            pass
        bad.append((p.relative_to(ROOT), len(hits)))
    return bad


def control():
    """Known-bad and known-good must classify correctly even when the tree is clean -- a disabled
    check and a check with nothing to find print the same PASS otherwise."""
    bad_ok = bool(BARE.search('f = it["record"].split("|")[0].strip()')) and bool(
        BARE.search('seen.add(m.group(1).split("|")[0].strip())'))  # the shape the first version missed
    good_ok = not BARE.search('parts = recfmt.fields(record)')
    exempt_ok = imports_recfmt(ast.parse("from recfmt import fields")) and not imports_recfmt(
        ast.parse("def recfmt():\n    pass"))
    return bad_ok and good_ok and exempt_ok


if __name__ == "__main__":
    if not control():
        print("FAIL: gate_bare_pipe's own predicate does not classify its controls"); sys.exit(1)
    print("  control: both known-bad shapes flagged (named and regex-extracted), known-good clean,")
    print("           and a function named recfmt does not exempt")
    bad = offenders()
    for f, n in bad:
        print(f"  {f}: {n} bare-pipe record split(s) -- use corpus/recfmt.fields()")
    if bad:
        print(f"FAIL: {len(bad)} file(s) parse a record span with the wrong separator"); sys.exit(1)
    print("PASS: every record-span parse goes through corpus/recfmt")
