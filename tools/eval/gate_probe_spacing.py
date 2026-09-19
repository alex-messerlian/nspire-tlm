#!/usr/bin/env python3
"""PERMANENT GATE: the probe's tool-call detection must be whitespace-insensitive.

THIS EXISTS BECAUSE ITS ABSENCE PUBLISHED A 0.0%. kprobe's D7 and D8 arms reported
`called 0/12` and `right target 0/12` on every run, which read as a model that had learned nothing
from 19,728 R1 and C1 training documents, and was reported as such. The model was emitting

    <tool> solve<arg> F=m*a<arg> m</tool><res> m=F/a</res><a> m=F/a -- same relation, m on its own.

which is exactly right. The tokenizer decodes the tags with leading spaces, and
`"<tool>solve<arg>" in out` is False against that string. Corrected on the same checkpoint: 50.0%
called, 50.0% right target.

The tell was inside the same function: the `stated result` arm three lines below already compared
with `.replace(" ", "")` and scored 50-58%. One comparison was normalised and its neighbours were
not, so the file disagreed with itself, and a 0% sitting beside a 50% on the SAME generations is
the signature of an instrument rather than an inability.

Both directions are asserted, because a gate that only checks the good string would pass if the
detection were reverted to the bare substring.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))

# The exact shape the tokenizer produces, taken from a real generation off train/math1.pt.
SPACED = ("<tool> solve<arg> F=m*a<arg> m</tool><res> m=F/a</res>"
          "<a> m=F/a -- same relation, m on its own.<end>")
TIGHT = "<tool>solve<arg>F=m*a<arg>m</tool><res>m=F/a</res><a>ok<end>"


def main():
    import kprobe
    sq = kprobe._squash

    if "<tool>solve<arg>" in SPACED:
        print("  CONTROL BROKEN: the spaced sample no longer needs squashing, so this gate is "
              "not testing what it claims. Re-derive it from a real generation.")
        return 2
    print("  control: the raw spaced generation does NOT contain the bare tag, as shipped")

    for name, s in (("spaced", SPACED), ("tight", TIGHT)):
        if "<tool>solve<arg>" not in sq(s):
            print(f"  FAIL: squashing the {name} generation does not expose the solve call. "
                  f"D7/D8 would report 0.0% for a model that called the tool correctly.")
            return 1
        arg = sq(s).split("<tool>solve<arg>", 1)[1].split("<arg>", 1)[0]
        if arg != "F=m*a":
            print(f"  FAIL: the {name} generation's first argument reads {arg!r}, not 'F=m*a', "
                  f"so `right target` would undercount.")
            return 1
    print("  both the spaced and the tight generation expose the call and the right target")

    # AND THE CALL SITES MUST ACTUALLY USE IT. Fixing the helper and leaving a bare substring in
    # the arm is the same defect with a helper sitting beside it.
    src = (ROOT / "tools/eval/kprobe.py").read_text()
    body = src[src.index("def main("):] if "def main(" in src else src
    bare = [ln.strip() for ln in body.splitlines()
            if re.search(r'"<tool>(solve|diff)<arg>"\s+in\s+out\b', ln)
            or re.search(r'\bout\.split\(\s*"<tool>(solve|diff)<arg>"', ln)]
    if bare:
        print("  FAIL: an arm still tests the raw generation rather than the squashed one:")
        for ln in bare:
            print(f"    {ln}")
        return 1
    print("  no arm tests the raw generation for a tool call")
    print("\n  PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
