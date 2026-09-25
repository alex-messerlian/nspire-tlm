#!/usr/bin/env python3
"""Did the model COMPUTE THE RIGHT ANSWER? The check no other arm in this repo makes.

    TOK=... .venv-tok/bin/python tools/eval/score_correct.py train/ship.pt [out.json]

WHY THIS FILE EXISTS. Every computation number quoted before it measured something narrower:
  - `d1` / `d1_zero` are REFUSAL arms: the record answers the question but a needed given is
    withheld, and the correct output is "I cannot answer that: F is not given." They were reported
    as "grounded computation" in FACTS.md, DRAFT.md and RESULT_WIDTH_LADDER.md.
  - score_arms' `answered` on answer_0/s/w/x is well_formed + not a refusal +
    answer_matches_result, and answer_matches_result only asks whether the stated answer is a
    correct rounding of WHATEVER the tool returned. A call with a wrong value, or the wrong
    relation, passes it.

DEFINITION. An item is CORRECT iff the generation is well-formed, is not a refusal, the stated
answer is a correct rounding of a <res> (grade.answer_matches_result), AND the LAST <res> is within
REL_TOL of the REFERENCE.

REFERENCE. The record's right-hand side with the question's givens substituted (and the store's
`cval` constants for any symbol the question does not give), executed by evalcli -- the device's
own evaluator, so there is no second implementation of the maths to disagree with the first.
Items whose reference cannot be formed are COUNTED AND PRINTED, never silently dropped.

CONTROLS, run on every invocation before any checkpoint is scored:
  positive  the canonical document for each item (the reference call, its real result, an answer
            restating it) must score CORRECT on every scoreable item;
  negative  the same document with one given scaled by 1.1 inside the call must score INCORRECT.
Either failing aborts, because a scorer that cannot tell those apart measures nothing.
"""
import json, os, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/eval"))
import grade                                                    # noqa: E402

ARMS = ("answer_0", "answer_s", "answer_w", "answer_x")
SEEDS = (1234, 5678, 9012)
REL_TOL = 1e-3      # givens are copied verbatim and the evaluator is deterministic, so a correct
                    # call reproduces the reference exactly; this tolerates formatting and a
                    # constant written to 4 s.f. (6.674e-11 vs 6.67e-11 is 6e-4), nothing more.

# A number, WITHOUT a trailing sentence period: `[\d.]+` captures "82.5." from "d_o = 82.5." and
# that exact bug has already produced a false failure in this repo (gate_split_valid).
NUM = r"[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?"
GIVEN = re.compile(rf"\b([A-Za-z][A-Za-z0-9_]*)\s*=\s*({NUM})")
RES = re.compile(r"<res>(.*?)</res>", re.S)
STORE = {r["f"]: r.get("cval") or {}
         for r in json.loads((ROOT / "corpus/store_clean.json").read_text())}


def evalcli(expr):
    p = subprocess.run([str(ROOT / "tools/eval/evalcli"), f"<tool>eval<arg>{expr}</tool>"],
                       capture_output=True, text=True)
    m = RES.search(p.stdout)
    return m.group(1).strip() if m else "!none"


def _num(s):
    m = re.search(NUM, s or "")
    try:
        return float(m.group()) if m else None
    except ValueError:
        return None


def substituted(item, scale=None):
    """The record's RHS with every known symbol replaced by (value). `scale` = (name, factor)
    perturbs one given, for the negative control."""
    formula = item["record"].split(" | ", 1)[0]
    rhs = formula.split("=", 1)[1]
    vals = dict(STORE.get(formula, {}))
    vals.update({k: v for k, v in GIVEN.findall(item["q"])})
    if scale and scale[0] in vals:
        vals[scale[0]] = repr(float(vals[scale[0]]) * scale[1])
    for name in sorted(vals, key=len, reverse=True):     # longest first; \b keeps Delta_s whole
        rhs = re.sub(rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])", f"({vals[name]})", rhs)
    return rhs


def reference(item):
    v = _num(evalcli(substituted(item)))
    return v if v is not None and v == v else None       # NaN is not a reference


def correct(generation, ref):
    if not (grade.well_formed(generation) and not grade.is_refusal(generation)
            and grade.answer_matches_result(generation)):
        return False
    res = RES.findall(generation)
    got = _num(res[-1]) if res else None
    if got is None:
        return False
    return abs(got - ref) <= REL_TOL * max(abs(ref), 1e-300)


def canonical(item, scale=None):
    expr = substituted(item, scale)
    r = evalcli(expr)
    return f"<tool>eval<arg>{expr}</tool><res>{r}</res><a>The result is {r}.<end>"


RID = {r["f"]: r["rid"] for r in json.loads((ROOT / "corpus/store_clean.json").read_text())}


def device_prompts(items):
    """The prompt the calculator builds for each item, via build/devasm (ask_build + ns_assemble,
    the shipped C). The split's own text is a different shape: the device strips the givens out of
    the question and re-appends them, and its record span carries the asked quantity's unit."""
    lines = "".join(f"{RID[it['record'].split(' | ', 1)[0]]}\t{it['q']}\n" for it in items)
    p = subprocess.run([str(ROOT / "build/devasm"), str(ROOT / "build/store.tns")],
                       input=lines, capture_output=True, text=True, check=True)
    out = p.stdout.splitlines()
    bad = [o for o in out if o.startswith("ERR ")]
    if len(out) != len(items) or bad:
        raise SystemExit(f"ABORT: devasm returned {len(out)} prompts for {len(items)} items, "
                         f"{len(bad)} failures ({bad[:3]})")
    return out


def load(arm, device=False):
    items = json.loads((ROOT / f"corpus/split_{arm}.json").read_text())
    prompts = (device_prompts(items) if device
               else [f"<q>{it['q']}</q><r>{it['record']}" for it in items])
    scoreable, skipped = [], []
    for it, pr in zip(items, prompts):
        it = dict(it, prompt=pr)
        ref = reference(it)
        (scoreable if ref is not None else skipped).append((it, ref))
    return scoreable, skipped


def controls(tables):
    for arm, (sc, _) in tables.items():
        pos = sum(correct(canonical(it), ref) for it, ref in sc)
        neg_n = neg_k = 0
        for it, ref in sc:
            givens = [k for k, _ in GIVEN.findall(it["q"])
                      if re.search(rf"(?<![A-Za-z0-9_]){re.escape(k)}(?![A-Za-z0-9_])",
                                   it["record"].split(" | ", 1)[0].split("=", 1)[1])]
            if not givens:
                continue
            neg_n += 1
            neg_k += not correct(canonical(it, (givens[0], 1.1)), ref)
        print(f"  control {arm:8s} positive {pos}/{len(sc)}   negative rejected {neg_k}/{neg_n}")
        if pos != len(sc):
            raise SystemExit(f"ABORT: positive control failed on {arm} -- the scorer rejects the "
                             "reference's own document.")
        if neg_k < 0.95 * neg_n:
            raise SystemExit(f"ABORT: negative control failed on {arm} -- a perturbed given passes.")


def greedy_generate(m, prompt, maxlen=160):
    """ARGMAX, as the calculator decodes (device_app.c: `tok = argmax(lg, V)`). score_arms samples
    at temperature 0.8 to match the corpus; that measures a decoder the device does not run. Same
    shared loop, same <res> mask, same tool runner -- only the sampling rule differs."""
    import torch, score_arms
    from genloop import generate_text
    TK = score_arms.TK

    def step(ids):
        lg = m(torch.tensor([ids[-256:]]))[:, -1, :]
        lg[0, score_arms.RES] = -1e30
        return lg[0]

    out = generate_text(step, lambda t: TK.encode(t).ids,
                        lambda i: TK.decode(i, skip_special_tokens=False),
                        TK.encode(prompt).ids, res_id=score_arms.RES, end_id=score_arms.ENDT,
                        toolc_id=score_arms.TOOLC, run_tool=score_arms._run_tool,
                        max_tokens=maxlen, sample=lambda logits: int(torch.argmax(logits)))
    return prompt, out


def score(ck_path, tables, greedy=False):
    import torch, score_arms                                    # after controls: import is slow
    ck = torch.load(ck_path, map_location="cpu", weights_only=False)
    from model import Transformer, ModelArgs
    m = Transformer(ModelArgs(**ck["args"]))
    m.load_state_dict(ck["model"]); m.eval()
    score_arms.pairing_smoke(m, ck)
    out = {"checkpoint": str(ck_path), "corpus_sha": ck.get("corpus_sha"),
           "tok_sha": ck.get("tok_sha"), "dim": ck["args"].get("dim"), "rel_tol": REL_TOL,
           "seeds": [0] if greedy else list(SEEDS), "decoding": "greedy" if greedy else "t0.8",
           "arms": {}}
    seeds = (0,) if greedy else SEEDS                 # greedy is deterministic: one pass is all
    gen = (lambda m_, p_: greedy_generate(m_, p_)) if greedy else score_arms.generate
    for arm, (sc, sk) in tables.items():
        per_seed, rows = [], []
        for seed in seeds:
            torch.manual_seed(seed)
            k = 0
            for it, ref in sc:
                with torch.no_grad():
                    _, gn = gen(m, it["prompt"])
                ok = correct(gn, ref)
                k += ok
                rows.append({"seed": seed, "prompt": it["prompt"], "ref": ref,
                             "gen": gn, "correct": ok,
                             "well_formed": grade.well_formed(gn),
                             "refused": grade.is_refusal(gn)})
            per_seed.append(k)
        n = len(sc)
        out["arms"][arm] = {"k": per_seed, "n": n, "skipped": len(sk),
                            "mean_pct": 100 * sum(per_seed) / (len(seeds) * n), "rows": rows}
        print(f"  {arm:8s} correct {sum(per_seed)/len(seeds):6.1f}/{n} = "
              f"{100*sum(per_seed)/(len(seeds)*n):5.1f}%  per seed {per_seed}  "
              f"(unscoreable, excluded and counted: {len(sk)})", flush=True)
    return out


def call_right(generation, ref):
    """The LAST <res> matches the reference, whatever the prose then says about it."""
    res = RES.findall(generation)
    got = _num(res[-1]) if res else None
    return got is not None and abs(got - ref) <= REL_TOL * max(abs(ref), 1e-300)


def score_int8(tables, model=None, label="train/ship.pt"):
    """THE CALCULATOR'S DECODER: build/int8gen runs the shipped int8 model through a copy of
    app_request's generation loop (tool injection, <res> mask, 90-step cap) and reports the shipped
    answer_states_result. Only train/ship.pt exists as an int8 file (build/transfer/, verified by cmp
    against the quantised checkpoint), so this scores the shipped model only.

    Two outcomes per item, because the device has a runtime check the model does not:
      correct       as everywhere else in this file: the model's own document is right
      value_shown   the call's result is right AND either the prose states it (the device's 2%
                    test) or the device appends "The calculator computed N" beneath it -- what the
                    student is shown, with a misstatement flagged rather than hidden."""
    model = model or ROOT / "build/transfer/model4096.bin.tns"
    tok = ROOT / "build/tok4096.tok"
    import struct
    dim = struct.unpack("<i", pathlib.Path(model).read_bytes()[8:12])[0]
    out = {"checkpoint": f"{label} (int8, {pathlib.Path(model).name})", "dim": dim,
           "rel_tol": REL_TOL, "seeds": [0],
           "decoding": "greedy-int8", "prompt_path": "device", "arms": {}}
    for arm, (sc, sk) in tables.items():
        p = subprocess.run([os.environ.get("INT8GEN", str(ROOT / "build/int8gen")), str(model), str(tok)],
                           input="".join(it["prompt"] + "\n" for it, _ in sc),
                           capture_output=True, text=True, check=True)
        lines = [l for l in p.stdout.splitlines() if l.startswith("GEN\t")]
        if len(lines) != len(sc):
            raise SystemExit(f"ABORT: int8gen returned {len(lines)} lines for {len(sc)} items")
        rows, k, shown, dev = [], 0, 0, 0
        for (it, ref), line in zip(sc, lines):
            _, text, res, states = line.split("\t")[:4]
            gn = text.replace("\\n", "\n").replace("\\t", "\t").replace("\\\\", "\\")
            ok = correct(gn, ref)
            vis = call_right(gn, ref) and "<a>" in gn          # prose right, or corrected
            # THE DEVICE'S OWN CRITERION for the prose: the shipped answer_states_result (a stated
            # number within 2% of the result). The strict grade.answer_matches_result rejects a
            # truncation (4737.6 -> "4737") and even a correct 4-sf "16670" for 16667.89, because it
            # counts an integer's trailing zero as significant.
            dok = (grade.well_formed(gn) and not grade.is_refusal(gn) and call_right(gn, ref)
                   and states == "1" and "<a>" in gn)
            k += ok; shown += vis; dev += dok
            rows.append({"seed": 0, "prompt": it["prompt"], "ref": ref, "gen": gn, "correct": ok,
                         "value_shown": vis, "device_ok": dok,
                         "prose_states_result": states == "1",
                         "well_formed": grade.well_formed(gn), "refused": grade.is_refusal(gn)})
        n = len(sc)
        out["arms"][arm] = {"k": [k], "n": n, "skipped": len(sk), "mean_pct": 100 * k / n,
                            "device_ok_pct": 100 * dev / n,
                            "value_shown_pct": 100 * shown / n, "rows": rows}
        print(f"  {arm:8s} correct(strict) {k}/{n} = {100*k/n:5.1f}%   correct(device check) "
              f"{dev}/{n} = {100*dev/n:5.1f}%   correct value shown {shown}/{n} = "
              f"{100*shown/n:5.1f}%", flush=True)
    return out


def breakdown(paths):
    """Where the non-correct items go. Every row lands in exactly one bucket, so they sum to n:
         correct          the call's result is right and the answer restates it correctly
         restated_wrong   the call's result is right and the answer misstates it
         call_wrong       well-formed, not a refusal, and the call's result is wrong
         refused          declined an answerable question
         malformed        none of the above: no well-formed document
       `answered` is score_arms' old definition, printed beside it for comparison."""
    for p in paths:
        r = json.loads(pathlib.Path(p).read_text())
        print(f"{pathlib.Path(r['checkpoint']).stem}  dim {r['dim']}  corpus {r.get('corpus_sha', '-')}"
              f"  prompts: {r.get('prompt_path', 'split')}  decoding: {r.get('decoding', 't0.8')}")
        for arm, a in r["arms"].items():
            b = dict.fromkeys(("correct", "restated_wrong", "call_wrong", "refused",
                               "malformed", "answered"), 0)
            for row in a["rows"]:
                g, ref = row["gen"], row["ref"]
                b["answered"] += (row["well_formed"] and not row["refused"]
                                  and grade.answer_matches_result(g))
                # RECOMPUTED from the saved generation, never read from the stored flag, so a grader
                # fix (e.g. the trailing-zero repair) reaches every saved run without regenerating.
                if correct(g, ref):
                    b["correct"] += 1
                elif row["refused"]:
                    b["refused"] += 1
                elif not row["well_formed"]:
                    b["malformed"] += 1
                elif call_right(g, ref):
                    b["restated_wrong"] += 1
                else:
                    b["call_wrong"] += 1
            tot = len(a["rows"])
            assert sum(v for k, v in b.items() if k != "answered") == tot
            print(f"  {arm:8s} n={a['n']}x{len(r['seeds'])}  " + "  ".join(
                f"{k} {100*v/tot:5.1f}%" for k, v in b.items()))


if __name__ == "__main__":
    if sys.argv[1:2] == ["--report"]:
        breakdown(sys.argv[2:])
        sys.exit(0)
    device, greedy = "--device" in sys.argv, "--greedy" in sys.argv
    int8 = "--int8" in sys.argv
    args = [a for a in sys.argv[1:] if a not in ("--device", "--greedy", "--int8")]
    tables = {arm: load(arm, device or int8) for arm in ARMS}
    controls(tables)
    if int8:
        # args: [out.json] for the shipped file, or <model.bin> <label> <out.json> for another
        if len(args) == 3:
            r = score_int8(tables, pathlib.Path(args[0]), args[1]); dest = args[2]
        else:
            r = score_int8(tables); dest = args[0] if args else None
        if dest:
            pathlib.Path(dest).write_text(json.dumps(r, indent=1))
            print(f"  -> {dest}")
        sys.exit(0)
    if not args:
        sys.exit(0)                                   # controls only
    print(f"  prompt path: {'DEVICE (build/devasm)' if device else 'split text'}; "
          f"decoding: {'greedy' if greedy else 't=0.8, 3 seeds'}", flush=True)
    r = score(args[0], tables, greedy)
    r["prompt_path"] = "device" if device else "split"
    if len(args) > 1:
        pathlib.Path(args[1]).write_text(json.dumps(r, indent=1))
        print(f"  -> {args[1]}")
