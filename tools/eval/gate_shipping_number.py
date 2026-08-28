#!/usr/bin/env python3
"""PERMANENT GATE: the Step 0 shipping number may never appear without its framing.

WHY. 3.00% is a LOWER BOUND from a read whose false negatives are uncontrolled, and its meaning
lives entirely in three qualifiers: the sample size, the interval, and the fact that two readers
missing the same defect produces a smaller number rather than a warning. Quoted bare it reads as
"the corpus is 97% clean", which is not what was measured.

This is the standing hazard from the refusal-rate figure -- "never quote 97.8% without the
96.6/85.4/91.0 spread" -- and that one is held by documentation alone, which the project log says will
lapse. This one trips instead.

WHAT IT CHECKS. In every tracked .md, any line stating the rate must carry, within a small window,
the sample size, the interval, and a lower-bound qualifier. A table row satisfies it if the table's
surrounding prose does.
"""
import pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
RATE = re.compile(r"\b3\.00\s?%")
NEED = {                       # qualifier -> what proves it is present
    "sample size":  re.compile(r"\bn\s*=\s*400\b|\b400 document|\bof 400\b|12\s*/\s*400"),
    "interval":     re.compile(r"1\.72.{0,12}5\.17|\[1\.72,\s*5\.17\]"),
    "lower bound":  re.compile(r"lower bound|false negatives are not controlled|not controlled", re.I),
}
WINDOW = 40                    # lines either side; a table row is framed by its section's prose


def tracked_markdown():
    out = subprocess.run(["git", "ls-files", "*.md"], cwd=ROOT,
                         capture_output=True, text=True).stdout.split()
    return [ROOT / p for p in out]


def main():
    files = tracked_markdown()
    if not files:
        print("  CANNOT CHECK: git ls-files returned no markdown. Refusing to report a pass.")
        return 2
    bad, cites = [], 0
    for f in files:
        try:    lines = f.read_text().splitlines()
        except (OSError, UnicodeDecodeError):  continue
        for i, line in enumerate(lines):
            if not RATE.search(line):  continue
            cites += 1
            ctx = "\n".join(lines[max(0, i - WINDOW): i + WINDOW + 1])
            missing = [name for name, rx in NEED.items() if not rx.search(ctx)]
            if missing:
                bad.append((f.relative_to(ROOT), i + 1, missing, line.strip()[:70]))
    print(f"  citations of the shipping number: {cites} across {len(files)} tracked documents")
    for path, ln, missing, txt in bad:
        print(f"  BARE  {path}:{ln}  missing {', '.join(missing)}")
        print(f"        {txt}")
    if bad:
        print(f"\n  FAIL: {len(bad)} citation(s) without the framing. 3.00% is a LOWER BOUND from")
        print("  n=400 with uncontrolled false negatives; quoted bare it reads as a clean-rate.")
        return 1
    if cites == 0:
        print("  CANNOT CHECK: the number appears nowhere. Not a pass -- nothing was checked.")
        return 2
    print("  PASS: every citation carries n, the interval, and the lower-bound qualifier")
    return 0


if __name__ == "__main__":
    sys.exit(main())
