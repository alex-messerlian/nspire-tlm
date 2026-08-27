#!/usr/bin/env python3
"""PERMANENT GATE: the <res> span must be the runtime's string, byte for byte.

<res> IS THE ARCHITECTURE'S CENTRAL MECHANISM. src/store/toolrun.c:34 writes `<res>%s</res>`
straight from the evaluator, and tools/eval/fmt.c prints at SIG_DIGITS 10. The generator rebound
`res` to 4 significant figures before interpolating it -- a sign-off that was about the ANSWER's
precision and silently took the injected result with it.

MEASURED: 52.88% of answerable documents carried a result string the shipped runtime does not
produce (`9.541e-32` where the device emits `9.54144e-32`), AND the answer restated <res>
byte-for-byte in 100% of them. So the corpus never demonstrated a rounding step, and at serve time
the model is handed a 10-digit result in a slot where it has only ever seen four -- having learned
that the answer is the res span copied.

The A9 pattern exactly: two producers of one field, one changed, the other not.

WHAT IT DOES NOT VERIFY: that the call is right, or that the answer's rounding is sensible. It
verifies that what the corpus injects is what the runtime injects.
"""
import importlib.util, io, contextlib, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CLI = ROOT / "tools/eval/evalcli"
N, SEED = 1500, 860213

if __name__ == "__main__":
    if not CLI.exists():
        print(f"CANNOT CHECK: {CLI} is not built. Run: make tools/eval/evalcli"); sys.exit(2)
    spec = importlib.util.spec_from_file_location("gen", ROOT / "corpus/generate.py")
    m = importlib.util.module_from_spec(spec)
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            spec.loader.exec_module(m)
    except SystemExit:
        pass
    out = m.gen(N, seed=SEED)
    docs = [d["text"] if isinstance(d, dict) else d for d in (out[0] if isinstance(out, tuple) else out)]

    calls, mine = [], []
    for t in docs:
        a = re.search(r"(<tool>.*?</tool>)<res>(.*?)</res>", t, re.S)
        if a: calls.append(a.group(1)); mine.append(a.group(2))
    if not calls:
        # ABSENCE IS FAILURE: no call/result pair would otherwise report a perfect zero.
        print("CANNOT CHECK: no document carried a call and a result span"); sys.exit(2)

    p = subprocess.run([str(CLI), "-"], input="\n".join(calls) + "\n",
                       capture_output=True, text=True, cwd=ROOT)
    runtime = re.findall(r"<res>(.*?)</res>", p.stdout, re.S)
    if len(runtime) != len(calls):
        print(f"CANNOT CHECK: evalcli returned {len(runtime)} results for {len(calls)} calls")
        sys.exit(2)

    bad = [(c, x, y) for c, x, y in zip(calls, mine, runtime) if x != y]
    print(f"  call/result pairs            {len(calls):,}")
    print(f"  <res> NOT THE RUNTIME'S      {len(bad)}  ({len(bad)/len(calls):.2%})")
    for c, x, y in bad[:4]:
        print(f"      {c[-50:]}\n        corpus  {x}\n        runtime {y}")
    if bad:
        print("\n  FAIL: the corpus injects a result the runtime does not produce. The model would")
        print("  train on one token shape in the <res> slot and meet another at inference.")
        sys.exit(1)
    print("\n  PASS: every <res> is the runtime's string verbatim")
