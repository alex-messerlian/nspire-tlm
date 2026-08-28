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
import random, json, math, re, sys, collections

LAST_EXCESS = {}   # label -> excess pp, filled by gate(); the ratchet below reads it

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

def real_vs_real_floor(eval_qs, seed=0):
    """The floor, MEASURED IN-RUN at the caller's sample size rather than hardcoded.

    A fixed margin is wrong here because separability depends on sample size, and the first
    version of this gate hardcoded 0.292 from a run whose two sides were NOT size-matched. At
    n=200 a side the same measurement gives 23.7 pp. A constant that is only valid at one sample
    size is a proxy for the property, not the property -- so the floor is now computed from two
    samples of genuine OpenStax prose, at whatever size the split under test happens to be.

    Returns None when corpus/clean_surface.json is absent, and the caller must then FAIL rather
    than fall back to a constant: "cannot check" and "checked and clean" must not share a result.
    """
    try:
        real = [x["stem"] for x in json.load(open("corpus/clean_surface.json"))["items"]]
        mined = [str(x) for x in json.load(open("corpus/stems_all.json"))]
    except (FileNotFoundError, KeyError):
        return None
    # D1. evaluate() ITSELF halves: `n = min(len(eval_qs), len(train_qs)//2)`. Passing real[:n]
    # and mined[:n] therefore measured the floor at n/2 while the gate ran at n -- the mismatched
    # sample-size defect this function's docstring announces as fixed, reproduced inside the fix.
    # Give the train side 2n so evaluate's own n lands on the caller's n, and ASSERT it did.
    n = min(len(eval_qs), len(real), len(mined) // 2)
    rng = random.Random(seed)
    # D3. Both sides are shuffled. Only `mined` was, so for the 80-item splits the floor saw the
    # first 80 of clean_surface.json forever, on every seed -- invisible while that file happens
    # to be in random order, and silently topic-specific the moment it is not.
    rng.shuffle(mined); rng.shuffle(real)
    r, c, got = evaluate(mined[:2 * n], real[:n], seed=seed)
    assert got == n, f"floor measured at n={got}, caller runs at n={n}"
    return r - c


def real_vs_real_floor_mean(eval_qs, seeds=5):
    """D2. A ONE-SEED THRESHOLD IS A COIN FLIP, and the noise is on the MARGIN, so no amount of
    averaging the measurement fixes it. Over 20 seeds the 80-item floor ranges +9.0 to +28.5 with
    sd 5.13 -- and seed 0, which the gate used, draws the maximum of the 20. A corpus that had
    genuinely improved by 8 pp would still have failed at seed 3.

    Returns (mean, sd, n_seeds), or (None, None, 0) when the inputs are absent.
    """
    xs = []
    for s in range(seeds):
        f = real_vs_real_floor(eval_qs, seed=s)
        if f is None: return None, None, 0
        xs.append(f)
    mean = sum(xs) / len(xs)
    sd = (sum((x - mean) ** 2 for x in xs) / max(1, len(xs) - 1)) ** 0.5
    return mean, sd, len(xs)


LAST_SEM = {}


def gate(train_qs, eval_qs, label, margin, seeds=5):
    """THE RATCHETED QUANTITY WAS A SINGLE DRAW COMPARED AGAINST A MULTI-SEED BASELINE.

    D2 fixed the FLOOR to a seed mean and left the EXCESS -- the number actually ratcheted -- at
    seed 0, while the BASELINE comment below says "These are 5-seed means, not the seed-0 draws
    the first set were". The two halves of one comparison were measured different ways, and the
    comment asserted the fix that had only been applied to the other half.

    Measured cost: excess sd is 1.8-2.4 pp over 10 seeds, range ~6.5 pp. THE UNMODIFIED HEAD TREE
    FAILED ITS OWN RATCHET -- SELECT +43.5 against a baseline of 38.5 -- with no corpus change of
    any kind, which is precisely the "trains everyone to ignore the gate" failure D2 named."""
    rs, cs, n = [], [], 0
    for s in range(seeds):
        r, c, n = evaluate(train_qs, eval_qs, seed=s)
        rs.append(r); cs.append(c)
    r, c = sum(rs) / len(rs), sum(cs) / len(cs)
    xs = [(a - b) * 100 for a, b in zip(rs, cs)]
    sd = (sum((x - sum(xs)/len(xs)) ** 2 for x in xs) / max(1, len(xs) - 1)) ** 0.5
    ok = (r - c) <= margin
    print(f"  {label:22} eval-vs-train {r:6.1%}   noise control {c:6.1%}   "
          f"excess {r-c:+6.1%} +-{sd:.1f}pp over {seeds} seeds   n={n}   "
          f"floor {margin:+5.1%}   {'PASS' if ok else 'FAIL'}")
    LAST_EXCESS[label] = (r - c) * 100.0
    LAST_SEM[label] = sd / (len(rs) ** 0.5)
    return ok

if __name__ == "__main__":
    tq = []
    for line in open("corpus/synth_sample.jsonl"):
        m = re.search(r"<q>(.*?)</q>", line, re.S)
        if m: tq.append(m.group(1))
        if len(tq) >= 20000: break
    print(f"  training questions sampled: {len(tq)}")
    ok = True
    splits = []
    for path, label in [("corpus/split_select.json", "SELECT"),
                        ("corpus/split_report.json", "REPORT"),
                        ("tools/eval/items.json",    "eval items.json")]:
        try: items = json.load(open(path))
        except FileNotFoundError: continue
        splits.append((label, [i["q"] for i in items]))

    # PER SPLIT, because the floor moves with n and the splits differ in size: SELECT is 80 items
    # and items.json is 200. One floor applied to both would be the hardcoded-constant bug again,
    # one level up.
    print("  floor: two samples of genuine OpenStax prose, measured in-run at EACH split's own")
    print("    sample size -- separability depends on n, so a single constant cannot serve both.")
    for label, qs in splits:
        floor, floor_sd, nseeds = real_vs_real_floor_mean(qs)
        if floor is not None:
            print(f"    {label:22} floor {floor:+6.1%} +- {floor_sd:.1%} over {nseeds} seeds")
        if floor is None:
            print("  CANNOT CHECK: corpus/clean_surface.json or corpus/stems_all.json is missing,")
            print("    so the floor cannot be measured. Refusing to substitute a constant.")
            sys.exit(2)
        ok &= gate(tq, qs, label, margin=floor)

    # A RATCHET AGAINST A RECORDED BASELINE, because the target is not reachable before the retrain.
    #
    # This gate was written with a __main__ and an exit code and was WIRED TO NOTHING -- the audit
    # found it PROSE_ONLY. Running it for the first time: the eval set is 91-95% separable from the
    # training corpus against a ~52% noise control, an excess of +37 to +45 pp against a 10 pp
    # margin. The name-interpolation hypothesis in the first version of this note was WRONG as a
    # sole cause: fixing it (A1) moved 1.31 pp of 45, and the corrected generator-vs-eval figure is
    # +43.5 (the +47.9 first published compared whole documents against bare question stems -- a
    # scope break worth 3.9 pp). See docs/CORPUS_PLAN.md sections 3, 4 and 8.
    #
    # AND THE GATE CANNOT SEE DIGITS. TOK is r"[a-z]+", so a corpus whose every number was rebound
    # to an absurd value scores IDENTICALLY here: measured across 448 digit-only rebinds, 448/448
    # strings changed and 0/448 feature streams did. Numeric plausibility is out of scope for this
    # instrument and currently has no owner.
    #
    # So the gate cannot pass today and must not therefore be ignored. It ratchets: the excess may
    # not get WORSE than the baseline below, and the baseline may only be lowered. That makes it
    # meaningful now, and it becomes a real pass/fail the moment the retrain lands.
    # Re-baselined 2026-08-27 after A6. These are 5-seed means, not the seed-0 draws the first
    # set were: a hardcoded seed-0 constant guarding a seed-dependent measurement fires "REGRESSION"
    # on a re-seed with no corpus change, which trains everyone to ignore the gate.
    # The TARGET is not a constant either -- it is the in-run floor printed above each row
    # (+22.3 at n=80, +24.2 at n=200, 5-seed). The retracted "29.2" is gone.
    # CORRECTED 2026-08-27, AND THIS IS A CORRECTION, NOT A RELAXATION. The 38.5/38.0 pair was
    # recorded as a 5-seed mean and cannot have been one: measured over 10 seeds, the generator AT
    # THAT COMMIT scores SELECT +42.6 +- 2.4 and REPORT +40.5 +- 2.4, and seed 0 alone gives +43.5
    # and +44.2. A ratchet the untouched tree fails is measuring its own seed, not the corpus.
    #
    # The evidence is a matched control, not an argument: two corpora, 30k documents each, one from
    # HEAD's generator and one from this one, same 10 seeds, same splits.
    #
    #                     SELECT                 REPORT
    #     HEAD      +42.6 +- 2.4 [39.0, 46.2]   +40.5 +- 2.4 [37.2, 44.2]
    #     current   +41.5 +- 1.9 [38.5, 45.0]   +39.6 +- 1.8 [36.8, 42.2]
    #
    # So these baselines are HEAD's measured means -- the real pre-change value -- and the rule
    # that they may only ever be LOWERED is unchanged and now actually enforceable.
    BASELINE = {"SELECT": 42.6, "REPORT": 40.5, "eval items.json": 45.5}   # pp; only ever LOWER
    print()
    print("  BASELINE RATCHET (this gate cannot pass before the corpus restart -- see the note in")
    print("  the source). Excess may not exceed these; lower them when the retrain improves matters:")
    worse = []
    for label, base in BASELINE.items():
        got = LAST_EXCESS.get(label)
        mark = ("n/a" if got is None else
                f"{got:+.1f} pp vs baseline {base:.1f}  (+-{2*LAST_SEM.get(label,0.0):.1f} pp, 2 sem)")
        # A RATCHET ON A NOISY QUANTITY MUST KNOW ITS OWN NOISE. Seed-averaging fixed the bias but
        # not the variance: this fired at +45.6 against a baseline of 45.5 -- a 0.1 pp exceedance on
        # a measurement whose 5-seed sem is ~0.7 pp. That is the same "fires on a re-seed with no
        # corpus change" failure one level down, and it is not fixed by moving the baseline, which
        # would be the relaxation this gate exists to prevent. It is fixed by requiring the increase
        # to be larger than the uncertainty in the increase: 2 standard errors of the mean.
        tol = 2.0 * LAST_SEM.get(label, 0.0)
        if got is not None and got > base + tol:
            worse.append(f"{label}: {got:+.1f} pp exceeds the baseline {base:.1f} pp "
                         f"by more than 2 sem ({tol:.1f} pp)")
        print(f"    {label:20} {mark}")
    if worse:
        print()
        for w in worse: print(f"  REGRESSION: {w}")
        sys.exit(1)
    print("  no split has regressed against its baseline")
    sys.exit(0)

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
