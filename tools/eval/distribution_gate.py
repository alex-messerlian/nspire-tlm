"""Predictive in-distribution gate: can a cheap classifier tell eval items from training items?

REPLACES the structural assertion `"fit:" in record and "missing:" in record`, which passed on a
split whose questions shared ZERO terms with their own records while training questions named the
record 94.9% of the time. A token-presence check cannot see a semantic break. This was written to
the letter of the anti-proxy rule and against its point.

Multinomial naive Bayes on word unigrams, hand-rolled (no sklearn on this host).

THE NEGATIVE CONTROL IS THE POINT. Any two finite samples separate a little, so an accuracy number
alone means nothing. The same classifier is trained to separate two RANDOM HALVES OF TRAINING from
each other; that is the accuracy achievable from noise at this sample size. The gate compares the
eval-vs-train accuracy against that control, not against 50%.

Does NOT verify: that a split which passes is usable, that the items are correct, or that the model
can answer them. It verifies only that a cheap bag-of-words model cannot tell the two sources
apart -- which is necessary for an in-distribution claim, not sufficient."""
import json, math, random, re, sys, collections

TOK = re.compile(r"[a-z]+")
def feats(s): return TOK.findall(s.lower())

def train_nb(pos, neg):
    cp, cn = collections.Counter(), collections.Counter()
    for s in pos: cp.update(feats(s))
    for s in neg: cn.update(feats(s))
    vocab = set(cp) | set(cn)
    tp, tn = sum(cp.values()) + len(vocab), sum(cn.values()) + len(vocab)
    lp = {w: math.log((cp[w] + 1) / tp) for w in vocab}
    ln = {w: math.log((cn[w] + 1) / tn) for w in vocab}
    return lp, ln, math.log(1 / tp), math.log(1 / tn)

def acc(model, pos, neg):
    lp, ln, dp, dn = model
    ok = 0
    for s, y in [(s, 1) for s in pos] + [(s, 0) for s in neg]:
        a = sum(lp.get(w, dp) for w in feats(s))
        b = sum(ln.get(w, dn) for w in feats(s))
        ok += int((a > b) == (y == 1))
    return ok / max(1, len(pos) + len(neg))

def split_half(xs, rng):
    xs = list(xs); rng.shuffle(xs); h = len(xs) // 2
    return xs[:h], xs[h:]

def evaluate(train_qs, eval_qs, seed=0, folds=5):
    """Returns (eval_vs_train accuracy, control accuracy, n). Both are held-out means."""
    rng = random.Random(seed)
    n = min(len(eval_qs), len(train_qs) // 2)
    real, ctrl = [], []
    for f in range(folds):
        tr = rng.sample(train_qs, 2 * n)
        ev = rng.sample(eval_qs, n)
        a_tr, b_tr = tr[:n], tr[n:]
        # real: eval vs train, held out
        cut = n // 2
        m = train_nb(ev[:cut], a_tr[:cut])
        real.append(acc(m, ev[cut:], a_tr[cut:]))
        # control: train-half vs train-half, identical shape and sample size
        m = train_nb(a_tr[:cut], b_tr[:cut])
        ctrl.append(acc(m, a_tr[cut:], b_tr[cut:]))
    return sum(real) / folds, sum(ctrl) / folds, n

def gate(train_qs, eval_qs, label, margin=0.10, seed=0):
    r, c, n = evaluate(train_qs, eval_qs, seed=seed)
    ok = (r - c) <= margin
    print(f"  {label:22} eval-vs-train {r:6.1%}   noise control {c:6.1%}   "
          f"excess {r-c:+6.1%}   n={n}   {'PASS' if ok else 'FAIL'}")
    return ok

if __name__ == "__main__":
    tq = []
    for line in open("corpus/synth_sample.jsonl"):
        m = re.search(r"<q>(.*?)</q>", line, re.S)
        if m: tq.append(m.group(1))
        if len(tq) >= 20000: break
    print(f"  training questions sampled: {len(tq)}")
    ok = True
    for path, label in [("corpus/split_select.json", "SELECT"),
                        ("corpus/split_report.json", "REPORT"),
                        ("tools/eval/items.json",    "eval items.json")]:
        try: items = json.load(open(path))
        except FileNotFoundError: continue
        ok &= gate(tq, [i["q"] for i in items], label)
    print(f"  margin: excess over the noise control must be <= 10.0 pp")
    sys.exit(0 if ok else 1)

# ---------------------------------------------------------------------------------------------
def record_recoverable(items, records, name_of, formula_of):
    """SHARPER CHECK: from the question alone, can the correct record be identified?

    The phrasing gate above flags any distribution difference, including a DELIBERATE held-out
    phrasing distribution, which is a feature and not a defect. It cannot tell intended holdout
    from the Finding B break. This one can: it asks whether the question is still coupled to its
    record at all.

    In training the answer is yes almost trivially -- generate.py interpolates the record's name
    into the question -- and that is exactly what makes the training number meaningless as
    evidence retrieval will work on real input. Reported here so the two are never conflated.

    Returns (top1, n). Chance is 1/len(records)."""
    idx = {formula_of(r): i for i, r in enumerate(records)}
    names = [set(TOK.findall(name_of(r).lower())) for r in records]
    hit = n = 0
    for it in items:
        f = it["formula"]
        if f not in idx: continue
        n += 1
        q = set(TOK.findall(it["q"].lower()))
        scores = [(len(q & nm) / max(1, len(nm)), -k) for k, nm in enumerate(names)]
        if max(range(len(scores)), key=lambda k: scores[k]) == idx[f]: hit += 1
    return hit / max(1, n), n
