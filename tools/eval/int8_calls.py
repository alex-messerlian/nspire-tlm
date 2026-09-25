#!/usr/bin/env python3
"""Does the INT8 ENGINE the calculator runs write the same tool call as the fp32 model we score?

    .venv-tok/bin/python tools/eval/int8_calls.py results/correct_devg_ship.json

score_correct.py scores the fp32 PyTorch model. The calculator runs the int8 C engine, which is
token-identical to the device on 208 logged tokens (docs/RESULT_HOST_DEVICE_PARITY.md) but has
never been compared with PyTorch. The step most errors come from is the first call -- copying the
givens into it -- and everything up to the first </tool> is decoded without any tool injection, so
build/golden_decode (the int8 engine on the host, greedy, the device's own prefill) reproduces that
step exactly.

For each item of the greedy device-path run it reports:
  agree       the int8 engine's first call is byte-identical to the fp32 model's
  int8 right  the int8 call, executed by evalcli, matches the reference (score_correct.REL_TOL)
  fp32 right  the same for the fp32 call
The model file is checked against the quantised train/ship.pt before use -- `cmp` identical was
established 2026-09-25 -- so this only applies to that checkpoint.
"""
import json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
import score_correct as S                                      # noqa: E402
from tokenizers import Tokenizer                               # noqa: E402

TK = Tokenizer.from_file(str(ROOT / "train/tok4096.json"))
MODEL = ROOT / "build/transfer/model4096.bin.tns"
TOKC = ROOT / "build/tok4096.tok"
CALL = re.compile(r"<tool>.*?</tool>", re.S)


def int8_generation(prompt):
    p = subprocess.run([str(ROOT / "build/golden_decode"), str(MODEL), str(TOKC), prompt],
                       capture_output=True, text=True)
    m = re.search(r"host ids\s*:(.*)", p.stdout)
    if not m:
        raise SystemExit(f"ABORT: golden_decode produced no ids for {prompt[:60]!r}: {p.stderr[:200]}")
    ids = [int(t) for t in m.group(1).split()]
    return TK.decode(ids, skip_special_tokens=False)


def first_call(text):
    m = CALL.search(text)
    return m.group(0) if m else None


def _exec_right(call, ref):
    """Execute the call through evalcli exactly as the runtime would, compare to the reference."""
    p = subprocess.run([str(ROOT / "tools/eval/evalcli"), call], capture_output=True, text=True)
    return S.call_right(p.stdout, ref)


def main(path):
    r = json.loads(pathlib.Path(path).read_text())
    assert r.get("prompt_path") == "device" and r.get("decoding") == "greedy", \
        "needs a greedy device-path run of score_correct.py"
    assert pathlib.Path(r["checkpoint"]).name == "ship.pt", "the int8 model on disk is train/ship.pt"
    for arm, a in r["arms"].items():
        n = agree = i8 = f32 = 0
        for row in a["rows"]:
            fc = first_call(row["gen"])
            ic = first_call(int8_generation(row["prompt"]))
            n += 1
            agree += (fc is not None and fc == ic)
            i8 += _exec_right(ic, row["ref"]) if ic else 0
            f32 += _exec_right(fc, row["ref"]) if fc else 0
        print(f"  {arm:8s} n={n}  calls identical {agree}/{n} = {100*agree/n:5.1f}%   "
              f"call right: int8 {100*i8/n:5.1f}%  fp32 {100*f32/n:5.1f}%", flush=True)


if __name__ == "__main__":
    main(sys.argv[1])
