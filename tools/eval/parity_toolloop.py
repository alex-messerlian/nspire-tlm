#!/usr/bin/env python3
"""Device/host parity of WHOLE TURNS: tool calls executed, results injected, refusals, explanations.

    .venv-tok/bin/python tools/eval/parity_toolloop.py results/genlog_battery_g32.txt

bench_generate's A152 phase logs, for each turn, the prompt the calculator assembled and every id
tlm_generate emitted (the model's tokens and the injected <res> spans). This replays each logged
prompt through build/int8gen -- the same src/store/gencore.c over the same runq_nspire.c, on the
host -- and compares the id sequences exactly. It also re-assembles each turn's prompt on the host
(build/devasm, from the same question and record) and checks it against the device's, so prompt
assembly and decoding are checked separately: a difference in one cannot hide inside the other.

Reports per turn: ids compared, identical or the first divergence, and the device's timing line.
Exit status is nonzero on any divergence or any turn that could not be compared.
"""
import json, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
import score_correct as S                                     # noqa: E402


def turns(log_text):
    """{turn: {kind, prompt, ids, timing}} from the LAST A152 run in the log."""
    runs = log_text.split("== generate ==")
    run = next((r for r in reversed(runs) if "turn 0 prompt:" in r), None)
    if run is None:
        raise SystemExit("ABORT: no A152 turn lines in the log -- was the new bench_generate run?")
    out = {}
    for key, field in (("kind", "kind"), ("prompt", "prompt"), ("ids", "ids"), ("timing", "timing")):
        for m in re.finditer(rf"^turn (\d+) {field}: (.*)$", run, re.M):
            out.setdefault(int(m.group(1)), {})[key] = m.group(2).rstrip()
    return out, run


def device_table():
    """The turns bench_generate runs, read from its OWN source -- the question list exists once."""
    src = (ROOT / "src/store/device_generate.c").read_text()
    body = src[src.index("static const struct { const char *kind, *f, *q; } TT[] = {"):]
    body = body[:body.index("};")]
    unq = lambda x: x.replace('\\"', '"').replace("\\\\", "\\")
    return [tuple(unq(g) for g in m) for m in
            re.findall(r'\{\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\s*\}', body)]


def main(path):
    T, run = turns(pathlib.Path(path).read_text())
    table = device_table()
    if len(table) != len(T):
        raise SystemExit(f"ABORT: {len(table)} turns in device_generate.c, {len(T)} in the log")
    host_prompts = S.device_prompts([{"q": q, "record": f} for _, f, q in table])
    asm_same = sum(hp == t.get("prompt") for hp, (_, t) in zip(host_prompts, sorted(T.items())))
    print(f"prompt assembly: host (build/devasm) equals the device's logged prompt on "
          f"{asm_same}/{len(T)} turns")
    if not re.search(r"battery, VALID", run):
        print("NOTE: this run is not marked battery-valid; parity does not depend on the clock, "
              "timings do")
    picks = [(t["kind"], t["prompt"]) for _, t in sorted(T.items()) if "prompt" in t and "ids" in t]
    if len(picks) != len(T):
        raise SystemExit(f"ABORT: {len(T) - len(picks)} turns lack a prompt or ids line")
    model = ROOT / "build/transfer/model4096.bin.tns"
    p = subprocess.run([str(ROOT / "build/int8gen"), str(model), str(ROOT / "build/tok4096.tok")],
                       input="".join(pr + "\n" for _, pr in picks), capture_output=True, text=True,
                       check=True)
    host = [l.split("\t") for l in p.stdout.splitlines() if l.startswith("GEN\t")]
    if len(host) != len(picks):
        raise SystemExit(f"ABORT: host produced {len(host)} turns for {len(picks)} prompts")
    total = same_turns = 0
    for (n, t), h in zip(sorted(T.items()), host):
        dev_ids = [int(x) for x in t["ids"].split()]
        host_ids = [int(x) for x in h[4].split()] if len(h) > 4 and h[4] else []
        k = min(len(dev_ids), len(host_ids))
        first = next((i for i in range(k) if dev_ids[i] != host_ids[i]), None)
        if first is None and len(dev_ids) != len(host_ids):
            first = k
        total += len(dev_ids)
        same_turns += first is None
        verdict = "IDENTICAL" if first is None else f"DIVERGE at id {first}"
        print(f"  turn {n} [{t['kind']:14s}] device {len(dev_ids):3d} ids, host {len(host_ids):3d}: "
              f"{verdict}")
        print(f"      {t.get('timing', '(no timing line)')}")
    print(f"whole turns identical: {same_turns}/{len(picks)}; device ids compared: {total}")
    return 0 if same_turns == len(picks) and asm_same == len(T) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
