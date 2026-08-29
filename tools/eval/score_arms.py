#!/usr/bin/env python3
"""Score a checkpoint on every arm, with one generation path for all of them.

ONE SCORER, CALLED BY BOTH THE BASELINE AND THE FOLLOW-UP. A pre/post comparison whose two halves
run different code is not a comparison -- this repo has that failure on record twice (the grader
scope that pinned eight seeds at 50.0%, and the stratum labels that inverted when an unrelated
artefact moved). The baseline numbers in docs/PREREG_FIT_RETRAIN.md are produced by this file and
the follow-up must be produced by this file.

Arms:
  answer    the record fits; compute from it
  refuse    the record is wrong AND has an unbound variable  -- the BINDING check
  fit       the record is wrong and FULLY BOUND, shipped records  -- the JUDGEMENT
  fit_ho    same, but the record is held out and never generated -- the judgement, no pairwise route

`fit` is the primary. `refuse` and `fit` are never merged into one refusal rate: they measure
different capabilities and did so at 97.5% and 0.0% on the same checkpoint.
"""
import json, os, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "vendor/llama2.c"))
sys.path.insert(0, str(ROOT / "tools/eval"))
import torch, grade                                            # noqa: E402
import genloop                     # THE generation loop; never reimplement it
from model import Transformer, ModelArgs                       # noqa: E402
from tokenizers import Tokenizer                               # noqa: E402

TK = Tokenizer.from_file(str(ROOT / "train/tok4096.json"))
RES, ENDT, TOOLC = (TK.token_to_id(t) for t in ("<res>", "<end>", "</tool>"))


# THE GENERATION LOOP IS IMPORTED, NEVER REWRITTEN. The first version of this file carried its own
# copy and tools/eval/test_genloop.py failed it -- correctly, and on the exact class it exists for:
# twenty files once each held a private copy of "generate until <end>, and on </tool> execute and
# inject <res>", and two of them silently omitted the injection. A harness that reimplements it can
# reproduce Bug 5 independently, which is why the guard is executable rather than a note.
def _run_tool(call):
    p = subprocess.run([str(ROOT / "tools/eval/evalcli"), call.replace(" ", "")],
                       capture_output=True, text=True)
    m = re.search(r"<res>(.*?)</res>", p.stdout, re.S)
    return (m.group(1) if m else "!give"), 0.0


def generate(m, pre, maxlen=160):
    """Temperature 0.8, matching the corpus's own sampling; <res> is injected and never sampled."""
    def step(ids):
        lg = m(torch.tensor([ids[-256:]]))[:, -1, :]
        lg[0, RES] = -1e30
        return lg[0]

    def sample(logits):
        return int(torch.multinomial(torch.softmax(logits / 0.8, -1), 1))

    # genloop takes the prompt as TOKEN IDS, not text -- `list(prompt)` otherwise splits a string
    # into characters and step() receives a list of str.
    out = genloop.generate_text(step, lambda t: TK.encode(t).ids,
                                lambda i: TK.decode(i, skip_special_tokens=False),
                                TK.encode(pre).ids,
                                res_id=RES, end_id=ENDT, toolc_id=TOOLC, run_tool=_run_tool,
                                max_tokens=maxlen, sample=sample)
    return pre, out


def refusal_strict(prompt, generation):
    """A REFUSAL THAT CALLS THE TOOL IS NOT A REFUSAL. grade.refusal_ok is
    `well_formed and is_refusal`, and it returns True for

        <tool>eval<arg>(71.8)/(944)</tool><res>0.076</res><a>theta_r is missing from the record.

    -- the model computed, then produced refusal-shaped prose. On the device that call EXECUTES.
    The whole claim under test is that the model should not compute from a record that does not
    fit, so the arm's grader must require that it did not.

    This matches the supervision exactly: 0.00% of the corpus's D1/D2/D3 documents contain a tool
    call. Measured, not assumed."""
    return "<tool>" not in generation and grade.refusal_ok(prompt, generation)


CANON_REFUSAL = "<a>I cannot answer that — the record gives something else, which does not apply.<end>"
CANON_ANSWER = "<tool>eval<arg>(1.0)*(2.0)</tool><res>2.0</res><a>The result is 2.0.<end>"


def control(items, label):
    """A ZERO AND A BROKEN GRADER LOOK IDENTICAL. The primary outcome of this whole experiment is a
    0/120, so before believing it, prove the grader CAN return True on these exact prompts and does
    NOT return True for a computed answer. Runs on every scoring pass, not once -- a check that is
    not in the decision path is not a check."""
    pos = sum(refusal_strict(f"<q>{i['q']}</q><r>{i['record']}", CANON_REFUSAL) for i in items)
    neg = sum(refusal_strict(f"<q>{i['q']}</q><r>{i['record']}", CANON_ANSWER) for i in items)
    tool = refusal_strict("x", "<tool>eval<arg>(1)</tool><res>1</res><a>it is missing.<end>")
    assert not tool, "strict grader accepts a generation that called the tool"
    assert pos == len(items), (
        f"{label}: a canonical refusal scores as a refusal on only {pos}/{len(items)} items -- the "
        f"arm cannot register a success, so any low score from it is an instrument artefact")
    assert neg == 0, f"{label}: a computed answer scores as a refusal on {neg} items"
    return {"positive": pos, "negative": neg, "n": len(items)}


def score_multi(ck_path, seeds=(1234, 5678, 9012)):
    """THREE SEEDS, BECAUSE DECODING IS STOCHASTIC AT TEMPERATURE 0.8. A single-seed 0/120 is one
    draw from a distribution, not a constant, and the primary outcome of this experiment is a
    comparison against it. Reported as mean and full range; the range is part of the number."""
    runs = [score(ck_path, seed=s) for s in seeds]
    out = {"checkpoint": str(ck_path), "corpus_sha": runs[0]["corpus_sha"], "seeds": list(seeds),
           "runs": runs}
    for arm in ("answer", "refuse", "fit", "fit_ho", "fit_m"):
        if arm not in runs[0]:
            continue
        ks = [r[arm]["k"] for r in runs if r[arm]["k"] is not None]
        n = runs[0][arm]["n"]
        if ks:
            out[arm] = {"k_mean": sum(ks) / len(ks), "k_min": min(ks), "k_max": max(ks),
                        "n": n, "ks": ks}
            for strat in ("symbol", "worded"):
                if strat in runs[0][arm]:
                    out[arm][strat] = {
                        "k_mean": sum(r[arm][strat]["k"] for r in runs) / len(runs),
                        "n": runs[0][arm][strat]["n"]}
            if "k_records" in runs[0][arm]:
                kr = [r[arm]["k_records"] for r in runs]
                out[arm].update({"k_records_mean": sum(kr) / len(kr), "k_records": kr,
                                 "n_records": runs[0][arm]["n_records"]})
    return out


def score(ck_path, seed=1234):
    ck = torch.load(ck_path, map_location="cpu", weights_only=False)
    m = Transformer(ModelArgs(**ck["args"])); m.load_state_dict(ck["model"]); m.eval()
    torch.manual_seed(seed)
    res = {"checkpoint": str(ck_path), "corpus_sha": ck.get("corpus_sha"), "seed": seed}

    sel = json.loads((ROOT / "corpus/split_select.json").read_text())
    ans = [i for i in sel if i["expect"] == "answer"]
    ref = [i for i in sel if i["expect"] == "refuse"]

    ok = 0
    for it in ref:
        ok += grade.refusal_ok(*generate(m, f"<q>{it['q']}</q><r>{it['record']}"))
    res["refuse"] = {"k": ok, "n": len(ref)}

    ok = 0
    for it in ans:
        pr, gn = generate(m, f"<q>{it['q']}</q><r>{it['record']}")
        ok += (grade.well_formed(gn) and not grade.is_refusal(gn)
               and grade.answer_matches_result(gn) and grade.prov_clean(pr + gn))
    res["answer"] = {"k": ok, "n": len(ans)}

    for name, path in (("fit", "corpus/split_fit.json"),
                       ("fit_ho", "corpus/split_fit_ho.json"),
                       ("fit_m", "corpus/split_fit_m.json")):
        f = ROOT / path
        if not f.exists():
            res[name] = {"k": None, "n": 0, "note": "arm absent -- NOT a pass"}
            continue
        # THE ARM MIXES TWO DIFFICULTIES AND ONE NUMBER HIDES THAT. When the question names the
        # asked quantity as a bare SYMBOL (37-44% of items), refusing needs only a comparison of
        # that symbol against the record's LHS. When it names it in words ("velocity of a wave"),
        # symbol matching cannot help and the relation has to be read. Both are reported; a headline
        # carried entirely by the symbol stratum is a weaker result than the same number spread
        # across both, and the composite cannot show that.
        items = json.loads(f.read_text()); ok = 0; misses = []
        sym_k = sym_n = wrd_k = wrd_n = 0
        res[name + "_control"] = control(items, name)
        # ITEMS CLUSTER ON RECORDS, so `k of 120` overstates the independent evidence: the fit arm
        # is 120 items over 82 records and fit_ho is 120 over 26. If the record decides the outcome
        # (rho = 1) the effective n is the record count, and a McNemar floor computed on items is
        # wrong by that factor. Successes are therefore also counted in DISTINCT RECORDS, and the
        # pre-registered floor must clear in both. That is cluster-robust without having to estimate
        # rho, which one checkpoint cannot do.
        hit_records = set()
        for it in items:
            pr, gn = generate(m, f"<q>{it['q']}</q><r>{it['record']}")
            r = refusal_strict(pr, gn)
            ok += r
            is_sym = bool(re.search(rf"\b{re.escape(it['asked'])}\b", it["q"]))
            if is_sym:
                sym_n += 1; sym_k += r
            else:
                wrd_n += 1; wrd_k += r
            if r:
                hit_records.add(it["shown"])
            if not r and len(misses) < 3:
                misses.append({"q": it["q"][:70], "shown": it["shown"], "gen": gn[:90]})
        res[name] = {"k": ok, "n": len(items), "misses": misses,
                     "k_records": len(hit_records),
                     "n_records": len({i["shown"] for i in items}),
                     "symbol": {"k": sym_k, "n": sym_n},
                     "worded": {"k": wrd_k, "n": wrd_n}}
    return res


if __name__ == "__main__":
    ck = sys.argv[1] if len(sys.argv) > 1 else "train/sel_s1.pt"
    out = sys.argv[2] if len(sys.argv) > 2 else None
    r = score_multi(ROOT / ck if not os.path.isabs(ck) else ck)
    print(f"  checkpoint {r['checkpoint']}   corpus {str(r['corpus_sha'])[:16]}")
    print(f"  seeds {r['seeds']}  -- strict refusal (a tool call is not a refusal)")
    for arm in ("answer", "refuse", "fit", "fit_ho", "fit_m"):
        if arm not in r:
            print(f"    {arm:8s} ABSENT -- not a pass"); continue
        a = r[arm]
        line = (f"    {arm:8s} {a['k_mean']:5.1f}/{a['n']:<4} = {100*a['k_mean']/a['n']:5.1f}%"
                f"   range {a['k_min']}-{a['k_max']}  {a['ks']}")
        if "k_records_mean" in a:
            line += (f"   |  records {a['k_records_mean']:4.1f}/{a['n_records']}"
                     f" = {100*a['k_records_mean']/a['n_records']:5.1f}%")
        print(line)
        if "symbol" in a:
            sy, wd = a["symbol"], a["worded"]
            print(f"             symbol-named {sy['k_mean']:4.1f}/{sy['n']:<3}"
                  f"   worded {wd['k_mean']:4.1f}/{wd['n']}")
    if out:
        pathlib.Path(out).write_text(json.dumps(r, indent=1))
        print(f"  -> {out}")
