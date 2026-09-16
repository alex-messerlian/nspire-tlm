#!/usr/bin/env python3
"""A function declared in app.h must have a caller. A dead public API reads as a shipped feature.

WHY. `app_context()` builds the conversation history for a follow-up question: three tiers, a
character budget, verbatim recent turns collapsing to summaries, a measured 25.2 -> 15.5 tokens per
turn. Fully implemented, carefully commented, and NOTHING CALLS IT. Every turn on the calculator is
turn one, and the user's requirement is "I can ask like one or two follow-up questions".

It read as finished because `tools/webui/server.py` carries a second implementation under a
docstring saying it matches the device exactly. True about the algorithm, false about the device.

SEVENTH INSTANCE OF THE UNWIRED CLASS in this repo, after provenance.c, dim_gate on absent input,
the base() ruling held by documentation, IN_SCROLL handled with no producer, two graders where one
was audited, and provcli with no build rule. docs/WIRING_AUDIT.md covers checks and event
PRODUCERS. It did not cover the public API surface. This does.

EXEMPTIONS CARRY A REASON AND ARE NOT THE DEFAULT. An open defect is on a list somebody re-reads; a
defect wrongly marked closed is actively protected from the next audit. `app_context` is therefore
listed as OPEN below, not exempted.

HOW IT ENFORCES, and this is gate_split_heldout's pattern rather than a new one. A gate that is
permanently red trains its readers to ignore it, and this repo has the receipt: an 18-minute
control meta-gate got killed mid-flight instead of waited for, and two overlapping runs then left
mutations in the tree. So:

  * a dead function NOT on either list          -> FAIL now. That is a new defect.
  * a dead function on the OPEN list            -> printed loudly every run, does not fail
  * an OPEN or EXEMPT entry that HAS a caller   -> FAIL, so the list cannot go stale
  * OPEN empty                                  -> the gate is fully enforcing, with nothing to do

The count prints on every run, so closing them is visible and re-opening one is not silent.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HDR = ROOT / "src/store/app.h"

# name -> why it legitimately has no caller. Empty dict is the honest starting state.
EXEMPT = {}

# name -> the document that records the defect. NOT exemptions: these print loudly on every
# run and are the list this gate exists to drive to empty. See the header for why they do
# not fail the suite today. A NEW dead declaration fails immediately.
OPEN = {
    "app_context": "docs/RESULT_FOLLOWUP_UNWIRED.md -- follow-up questions are not wired to the "
                   "device, and the corpus has 0 two-turn documents, so both halves ship together",
    "app_hit_control": "docs/RESULT_FOLLOWUP_UNWIRED.md -- found by the same sweep; app_event "
                       "already routes clicks, so this is probably deletable rather than wirable",
}


def declared(hdr):
    """Function names declared in the header. Prototypes only: `... name(...);`"""
    out = []
    for m in re.finditer(r"^(?!\s*(?:typedef|#))[A-Za-z_][\w \t\*]*?\b(\w+)\s*\([^;{]*\)\s*;",
                         hdr, re.M):
        out.append(m.group(1))
    return sorted(set(out))


def callers(name, sources):
    """Files containing a CALL to `name`, excluding its own definition and its prototype."""
    hits = []
    defn = re.compile(r"^\s*(?:static\s+)?[A-Za-z_][\w \t\*]*\b" + re.escape(name) + r"\s*\([^;]*$")
    for path, text in sources.items():
        for line in text.split("\n"):
            if name + "(" not in line.replace(" (", "("):
                continue
            s = line.strip()
            if s.endswith(";") and defn.match(line.rstrip(";")):
                continue                       # a prototype
            if defn.match(line):
                continue                       # the definition
            if re.search(r"\b" + re.escape(name) + r"\s*\(", line):
                hits.append(path)
                break
    return hits


def main():
    hdr = HDR.read_text()
    sources = {}
    for d in ("src", "tools"):
        for p in (ROOT / d).rglob("*.c"):
            sources[str(p.relative_to(ROOT))] = p.read_text(errors="ignore")

    names = declared(hdr)
    dead = [n for n in names if not callers(n, sources)]

    # POSITIVE CONTROL. A clean tree and a broken predicate print the same thing, so the gate
    # proves on every run that it can tell a called function from an uncalled one.
    live = [n for n in names if n not in dead]
    if not live or not dead and not OPEN:
        print("  gate_no_dead_api: cannot run its own control"); return 1
    probe = "app_hit_stop"
    if probe in names:
        ok = bool(callers(probe, sources))
        if not ok:
            print(f"  FAIL: the predicate says {probe} has no caller, and it has several. "
                  f"Check the oracle before the subject."); return 1
        print(f"  control: {probe} is correctly seen as called; "
              f"{len(live)} of {len(names)} declarations have callers")

    rc = 0
    for n in dead:
        if n in EXEMPT:
            print(f"  exempt  {n:20s} {EXEMPT[n]}")
        elif n in OPEN:
            # Recorded, not enforced: see the header. It prints every run so it cannot be forgotten.
            print(f"  OPEN    {n:20s} {OPEN[n]}")
        else:
            print(f"  FAIL    {n:20s} declared in app.h, called by nothing, and undeclared as "
                  f"either exempt or a known defect")
            rc = 1
    for n in list(EXEMPT) + list(OPEN):
        if n not in dead:
            print(f"  FAIL    {n:20s} listed here but it HAS a caller now. Remove the entry: a "
                  f"stale exemption is how a real defect gets protected from the next audit.")
            rc = 1
    if rc == 0:
        n_open = sum(1 for n in dead if n in OPEN)
        if n_open:
            print(f"  PASS gate_no_dead_api: no NEW dead declarations. {n_open} known and open, "
                  f"listed above. The gate enforces fully once that list is empty.")
        else:
            print(f"  PASS gate_no_dead_api: every one of {len(names)} app.h declarations "
                  f"has a caller")
    return rc


if __name__ == "__main__":
    sys.exit(main())
