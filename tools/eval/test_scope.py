"""PERMANENT SUITE: every grader check, against both scopes.

Exists because a scope mismatch produced a published number twice. `grade.REF` contains
`\\bmissing\\b` and every record carries a `missing:` field, so a full document reads as a refusal;
provenance needs the question and record, so a bare generation reads as unsourced. The two
scope-sensitive checks are sensitive in OPPOSITE directions -- no single string satisfies both, and
any caller passing one string is wrong about one of them.

Run before trusting any number that comes out of grade.py. Exit 1 on any regression."""
import sys
sys.path.insert(0, "tools/eval")
import grade

REC = "v=d/t | v:m/s d:m t:s | missing:none | constant speed | fit:high"
P   = f"<q>A sled goes 84 m in 7 s.</q><r>{REC}"
G_OK   = "<tool>eval<arg>84/7</arg></tool><res>12</res><a> The speed is 12 m/s.<end>"
G_REF  = "<a> I cannot answer, a required value is missing.<end>"
G_FAB  = "<tool>eval<arg>84/7</arg></tool><res>12</res><a> The speed is 99 m/s.<end>"
G_ARG  = "<tool>eval<arg>7/3</arg></tool><res>2.333</res><a> The speed is 2.333 m/s.<end>"
G_MAL  = "<tool>eval<arg>84/7</arg></tool><res>12</res> the speed is 12"

CASES = [
    ("correct answer",        P, G_OK,  dict(ans=True,  ref=False)),
    ("genuine refusal",       P, G_REF, dict(ans=False, ref=True)),
    ("fabricated answer num", P, G_FAB, dict(ans=False, ref=False)),
    ("invented call arg",     P, G_ARG, dict(ans=False, ref=False)),
    ("malformed, no <a>",     P, G_MAL, dict(ans=False, ref=False)),
]

fail = 0
for name, p, g, want in CASES:
    got = dict(ans=grade.answer_ok(p, g), ref=grade.refusal_ok(p, g))
    if got != want:
        print(f"FAIL {name}: want {want} got {got}"); fail += 1

# The scope hazard itself, asserted directly in both directions.
if not grade.is_refusal(REC):
    print("FAIL a record contains 'missing:' and must trip the refusal pattern -- "
          "if this stops holding the hazard has moved, not vanished"); fail += 1
if grade.is_refusal(P + G_OK) is False:
    print("FAIL expected the full-document hazard to be demonstrable"); fail += 1
if not grade.prov_clean(P + G_OK):
    print("FAIL provenance must be clean on the FULL document"); fail += 1
if grade.prov_clean(G_OK):
    print("FAIL provenance on a bare generation must NOT read clean -- "
          "question-sourced numbers are invisible without the prompt"); fail += 1

print(f"{'FAILED' if fail else 'PASS'}: {len(CASES)} answer/refusal cases + 4 scope assertions, {fail} failure(s)")
sys.exit(1 if fail else 0)
