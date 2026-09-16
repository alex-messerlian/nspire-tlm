#!/usr/bin/env python3
"""Assert the five properties `corpus/extract_knowledge.py` revision 2 was written to hold.

Each fix closed a defect that EVERY structural check in this repo passed, because each produces a
well-formed string. A truncated section is a valid string. A teacher note is a valid paragraph. A
definition with its formula deleted is a valid definition. So the checks here read the CONTENT.

ONE CONTROL PER CONDITION, per the project log: a gate that checks five things behind one control proves
one thing. `--control` mutates the extractor's own inputs and asserts each check fails, so a check
that has quietly stopped being able to fire is reported rather than counted as a pass.
"""
import json, pathlib, re, subprocess, sys, collections

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFS = ROOT / "corpus/knowledge/definitions.json"
SECS = ROOT / "corpus/knowledge/sections.json"

TEACHER_MARK = re.compile(r"\[(BL|OL|AL|EL)\]")
# Instructor voice that survives even when the [XX] marker does not. Measured before it is trusted:
# the firing rate is printed, because a rule that fires on nothing is decorative and a rule that
# fires on everything is the wrong mechanism.
INSTRUCTOR = re.compile(
    r"\b(ask (the )?students|have (the )?students|tell students|remind students|"
    r"students should|encourage students|explain to (the )?students|"
    r"this (activity|demonstration) (will|can)|before (the )?class)\b", re.I)
FILLER_TITLE = re.compile(
    r"^\s*(test prep|critical thinking|problem exercises|additional problems|"
    r"challenge problems|practice problems|concept items|about openstax|errata)", re.I)


def check(secs, defs):
    """Return {name: (ok, detail)} for the five properties. Pure, so --control can reuse it."""
    out = {}

    # F1 -- no fixed-length truncation. The signature of the old defect is a spike at one length.
    lens = [len(s["text"]) for s in secs]
    spike = collections.Counter(lens).most_common(1)[0] if lens else (0, 0)
    frac = spike[1] / len(lens) if lens else 0
    out["F1_no_truncation"] = (
        frac < 0.02,
        f"most common length {spike[0]} occurs {spike[1]}x ({100*frac:.1f}% of {len(lens)}); "
        f"a spike over 2% means a cap is back")

    # F2 -- exercise and test-prep sections are gone.
    bad = [s["title"] for s in secs if FILLER_TITLE.match(s["title"])]
    out["F2_no_exercise_sections"] = (not bad, f"{len(bad)} filler-titled sections: {bad[:4]}")

    # F3 -- front matter is gone.
    fm = [s for s in secs if s.get("book_section", "").strip().lower() in
          {"preface", "about openstax", "acknowledgements", "acknowledgments"}]
    out["F3_no_front_matter"] = (not fm, f"{len(fm)} front-matter sections")

    # F4 -- no instructor-edition text, by marker AND by voice.
    mk = [s for s in secs if TEACHER_MARK.search(s["text"])]
    iv = [s for s in secs if INSTRUCTOR.search(s["text"])]
    out["F4_no_teacher_notes"] = (
        not mk and not iv,
        f"{len(mk)} carry a [BL]/[OL]/[AL]/[EL] marker, {len(iv)} carry instructor voice "
        f"(firing rate {100*len(iv)/max(1,len(secs)):.2f}%)")

    # F5 -- math survived, and the alphabet matches the shipped store.
    amputated = [d for d in defs if len(d["meaning"]) < 20 or d["term"].startswith("-")]
    nonascii = collections.Counter(
        ch for d in defs for ch in d["meaning"] + d["term"] if ord(ch) > 127)
    nonascii.update(ch for s in secs for ch in s["text"] + s["title"] if ord(ch) > 127)
    out["F5_math_rendered"] = (
        not amputated and not nonascii,
        f"{len(amputated)} amputated entries; {sum(nonascii.values())} non-ASCII chars over "
        f"{len(nonascii)} distinct {[hex(ord(c)) for c in list(nonascii)[:6]]}")
    return out


def main():
    if not DEFS.exists() or not SECS.exists():
        print("FAIL gate_knowledge: run corpus/extract_knowledge.py first")
        return 1
    defs, secs = json.load(open(DEFS)), json.load(open(SECS))
    res = check(secs, defs)

    if "--control" in sys.argv:
        # Every check must FAIL on an input carrying the defect it exists for. A check that passes
        # its own known-bad has stopped being able to fire.
        cases = {
            "F1_no_truncation":      (lambda: ([dict(s, text=s["text"][:900].ljust(900, "x"))
                                                for s in secs], defs)),
            "F2_no_exercise_sections": (lambda: (secs + [dict(secs[0], title="Test Prep for AP Courses")], defs)),
            "F3_no_front_matter":    (lambda: (secs + [dict(secs[0], book_section="Preface")], defs)),
            "F4_no_teacher_notes":   (lambda: (secs + [dict(secs[0],
                                        text="[BL] Ask students to recall the metric units.")], defs)),
            "F5_math_rendered":      (lambda: (secs, defs + [dict(defs[0], term="-particle",
                                        meaning="doubly ionized α atom")])),
        }
        rc = 0
        for name, mk in cases.items():
            s2, d2 = mk()
            ok, _ = check(s2, d2)[name]
            print(f"  control {name:26s} {'SURVIVED (gate cannot fire)' if ok else 'fires'}")
            rc |= 1 if ok else 0
        print("FAIL: a control survived" if rc else "all controls fire")
        return rc

    rc = 0
    for name, (ok, detail) in res.items():
        print(f"  {'PASS' if ok else 'FAIL'} {name:26s} {detail}")
        rc |= 0 if ok else 1
    print(f"{'PASS' if rc == 0 else 'FAIL'} gate_knowledge "
          f"({len(defs):,} definitions, {len(secs):,} sections)")
    return rc


if __name__ == "__main__":
    sys.exit(main())
