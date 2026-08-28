#!/usr/bin/env bash
# Full gate suite. Run after EVERY repair batch, not only after a review batch -- a repair gets
# the same scrutiny as a finding. Three records had their units invalidated by an LHS repair and
# stayed invisible for turns because nothing re-ran the gates afterwards.
#
# Run `make check` rather than this script directly: it BUILDS the host binaries first. Invoked on
# its own against a fresh clone, every ./build/test_* below reports "FAIL (not built)" -- correct,
# and not what you were trying to find out.
#
# EXIT STATUS IS THE RESULT. 0 = all gates ran and passed. Any non-zero = stop.
# Exit 2 from a gate means CANNOT CHECK, which is not the same as clean and is also a failure here.
# REFUSE TO RUN WHILE gate_controls.py IS MUTATING THE TREE. gate_controls deliberately breaks a
# source file, runs one gate, and restores it. This suite reading a file mid-mutation produces a
# failure that has nothing to do with the tree you are testing -- observed: shape_spec FAILED here
# and passed 9/9 standing alone, seconds later, because a control had corpus/generate.py broken at
# that instant. A red suite that is not about your change is worse than no suite: you go looking
# for a defect that does not exist.
#
# gate_controls took an exclusive lock to stop ITSELF racing; this is the other half. The lock
# protects the tree from two writers, and this protects a reader from the writer.
# ...AND IT MUST LET THE LOCK HOLDER THROUGH. gate_controls runs THIS SCRIPT to decide whether a
# gate fires, while holding the lock. The first version of this guard refused it, so all 23 gates
# reported ABSENT and the meta-gate said "23 without a proven control" -- a guard that protects a
# reader from a writer must not block the writer's own reads. gate_controls exports its pid; only
# that pid's descendants bypass, so a stale variable in an unrelated shell cannot disable this.
_LOCK="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/.gate_controls.lock"
if [ -f "$_LOCK" ] && [ "${TLM_CONTROLS_OWNER:-}" != "$(cat "$_LOCK" 2>/dev/null)" ] \
   && kill -0 "$(cat "$_LOCK" 2>/dev/null)" 2>/dev/null; then
  echo "REFUSING TO RUN: gate_controls.py (pid $(cat "$_LOCK")) is mutating this tree."
  echo "  Any failure here would be an artefact of its mutations, not of your change."
  exit 2
fi

set -u
STORE="${1:-corpus/store_clean.json}"
PY=.venv-tok/bin/python
fail=0

# ---- PREREQUISITES, NAMED --------------------------------------------------------------------
# The suite already separates exit 2 (CANNOT CHECK) from a real assertion failure. It did NOT
# separate a MISSING INTERPRETER or a MISSING INPUT, and both surface as an ordinary FAIL: in a
# fresh worktree, `.venv-tok/` and `build/store.tns` are gitignored and absent, so nine gates
# reported FAIL while every one of them passes the moment its input exists. That is the suite's own
# stated rule -- "cannot check and checked-and-clean must never share an exit status" -- violated
# one level up, at the prerequisites rather than at the gates.
#
# A missing prerequisite is still a non-zero exit. It is not a pass. It is just not a FINDING, and
# reporting it as one sends the reader looking for a defect in the records.
prereq_missing=0
need_file() {   # need_file <path> <how to make it>
    [ -e "$1" ] || { printf "  %-20s PREREQUISITE MISSING: %s\n" "$(basename "$1")" "$2"; prereq_missing=1; }
}
need_file "$PY"              "python3 -m venv .venv-tok && .venv-tok/bin/pip install -r requirements.txt"
need_file "$STORE"           "the record store; pass a path as \$1 if it lives elsewhere"
need_file build/store.tns    "python3 tools/store_pack.py $STORE build/store.tns"
need_file build/tok4096.tok  "python3 tools/tok_pack.py"
need_file tools/eval/shapecli "make tests   # the structural call check, ARCHITECTURE.md s6"
if [ "$prereq_missing" -ne 0 ]; then
    echo "GATE SUITE DID NOT RUN -- prerequisites above are missing."
    echo "This is NOT a gate failure. Nothing was checked. Create them and re-run."
    exit 3
fi
for g in lhs_gate lint_leibniz lint_declaration lint_fused_words dim_gate; do
    out=$($PY "tools/eval/$g.py" "$STORE" 2>&1); rc=$?
    case $rc in
      0) printf "  %-20s PASS\n" "$g" ;;
      2) printf "  %-20s CANNOT CHECK (counts as failure)\n" "$g"; fail=1 ;;
      *) printf "  %-20s FAIL (exit %d)\n" "$g" "$rc"; echo "$out" | head -4 | sed 's/^/      /'; fail=1 ;;
    esac
done
$PY tools/eval/test_scope.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_scope" || { printf "  %-20s FAIL\n" "test_scope"; fail=1; }
# The structural-shape rule (docs/ARCHITECTURE.md s6) validated against all nine known traces,
# including the two that provenance and dim_gate are both documented as unable to see. Listed here
# because a rule nothing runs is a rule that will drift from the C that implements it.
$PY tools/eval/shape_spec.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "shape_spec" || { printf "  %-20s FAIL\n" "shape_spec"; fail=1; }
# The C implementation, and a mutation pass over it. All-green on a first run triggers the mutation
# pass, not confidence -- this repo has three recorded cases of a suite that passed everything while
# measuring nothing.
$PY tools/eval/shape_mutation.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "shape_mutation" || { printf "  %-20s FAIL\n" "shape_mutation"; fail=1; }
# The project log names test_genloop.py as the EXECUTABLE guard that makes the Bug 5 ruling hold -- "the
# guard is executable and mutation-tested in both directions, which is what makes it hold". It was
# never listed here, so the guard that documentation could not provide was itself ungated.
$PY tools/eval/test_genloop.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_genloop" || { printf "  %-20s FAIL\n" "test_genloop"; fail=1; }
# score.py is the FOURTH grader -- it scores the 200-item eval set and was in no gate. It graded the
# executed result against the recorded reference and nothing else, so a call that invented its
# operands and landed on the right number passed. test_score also asserts that score.py and grade.py
# AGREE, because two graders means every check has to be added twice or it covers half the surface.
$PY tools/eval/test_score.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_score" || { printf "  %-20s FAIL\n" "test_score"; fail=1; }
# No tracked binary may exist without a rule that rebuilds it. provcli -- the check the
# architecture's central claim rests on -- was one, so a fix to provenance.c never reached the
# running binary. Third instance of the class after the four gate suites and build/asmcli.
$PY tools/eval/gate_binaries.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "gate_binaries" || { printf "  %-20s FAIL\n" "gate_binaries"; fail=1; }
# ITEM 14: every event kind app.c HANDLES must have a producer. WIRING_AUDIT's own closing
# instruction, never carried out -- and IN_SCROLL is still handled and emitted by nothing.
$PY tools/eval/gate_event_producers.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "event_producers" || { printf "  %-20s FAIL\n" "event_producers"; fail=1; }
# ITEM 15: distribution_gate.py was written with a __main__ and an exit code and wired to NOTHING.
# Its first run says the eval set is 91-95% separable from training against a ~52% noise control.
# Unreachable before the corpus restart, so it ratchets against a recorded baseline.
# A6: 22.5% of the shipped corpus asked for a quantity the record does not compute.
# Found by reading one generated question; every other gate passed those documents.
# A3+A4: a derived figure must still match the artefact it came from. chars/token was written
# as 3.5, corrected to 2.69 in prose in another document, and both were stale at 2.901.
# items.json ref is DERIVED by executing calls. Commit 25393cc edited q/record/calls by hand
# to settle three spelling decisions and ref went stale on 7 items -- two of which a correct
# model could then not pass.
$PY tools/eval/gate_items_refs.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "items_refs" || { printf "  %-20s FAIL\n" "items_refs"; fail=1; }
# DIMENSIONLESS_AUDIT.md calls this "gating (exit 1 on any)". It was in no gate, and it ran
# `./evalcli` relative, so it only worked from tools/eval. Both fixed 2026-08-27.
$PY tools/eval/audit_dimensionless.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "dimensionless" || { printf "  %-20s FAIL\n" "dimensionless"; fail=1; }
# topic-scoping-artifact, promoted from UNENFORCED 2026-08-27: a published selection improvement
# must publish its same-size random control. Topic-scoping read 61.2% against a random-20 at 62.7%.
$PY tools/eval/gate_selection_control.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "selection_control" || { printf "  %-20s FAIL\n" "selection_control"; fail=1; }
$PY tools/eval/gate_stale_figures.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "stale_figures" || { printf "  %-20s FAIL\n" "stale_figures"; fail=1; }
# A7: 16.2% of documents used a constant in the CALL that appeared in neither the question nor
# the record -- recalled, not read -- and while it was absent the graders could not tell a
# correct constant from a fabricated one (both "unchecked").
# A8: the D2 branch kept its own copy of the units-field rule, so fit:low was 100% predictable
# from a one-bit formatting cue and every D2 refusal metric measured the cue.
$PY tools/eval/gate_fit_cue.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "fit_cue" || { printf "  %-20s FAIL\n" "fit_cue"; fail=1; }
$PY tools/eval/gate_no_orphan_values.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "no_orphan_values" || { printf "  %-20s FAIL\n" "no_orphan_values"; fail=1; }
# R_train superseteq R_store: 25 of 166 records were retrievable and never trained, worth
# 12.2% vs 41.0% correct. The only property with a measured effect on correctness.
# store_clean and units_train both carry units; the generator prefers units_train, so a fix
# applied to only one silently does not propagate. The temperature-in-seconds fix did exactly that.
$PY tools/eval/gate_units_parity.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "units_parity" || { printf "  %-20s FAIL\n" "units_parity"; fail=1; }
# Physically impossible RESULTS -- the class nothing owned, because distribution_gate cannot
# see digits and dim_gate finds -28.75 a dimensionally fine efficiency.
# A21: gate_plausible filters RESULTS; every GIVEN was unchecked, and 69% of trig documents fed
# an angle of several full turns. Range is per QUANTITY, not per unit.
# Seven declared givens were drawn as 0.001 in 100% of documents: the window contained exactly
# one pool value. A variable that never varies is invisible to every range check.
$PY tools/eval/gate_no_collapse.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "no_collapse" || { printf "  %-20s FAIL\n" "no_collapse"; fail=1; }
$PY tools/eval/gate_given_range.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "given_range" || { printf "  %-20s FAIL\n" "given_range"; fail=1; }
$PY tools/eval/gate_plausible.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "plausible" || { printf "  %-20s FAIL\n" "plausible"; fail=1; }
# THE SAME DEFECT WAS FOUND FOUR TIMES, ONE FIELD OVER EACH TIME (units, condition, fit,
# missing). This compares the whole record span BYTE FOR BYTE against build/asmcli, so it
# cannot be outflanked by a field nobody thought of.
# <res> is the architecture's central mechanism: toolrun.c writes the evaluator string
# verbatim at SIG_DIGITS 10, and the generator was rounding it to 4 s.f. -- 52.88% of docs.
$PY tools/eval/gate_res_verbatim.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "res_verbatim" || { printf "  %-20s FAIL\n" "res_verbatim"; fail=1; }
$PY tools/eval/gate_record_bytes.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "record_bytes" || { printf "  %-20s FAIL\n" "record_bytes"; fail=1; }
# A bulk edit doubled corpus/generate.py (1,070 -> 1,565 lines) and EVERY functional check
# passed -- Python takes the later definition. Only the mutation meta-gate noticed.
$PY tools/eval/gate_no_dup_defs.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "no_dup_defs" || { printf "  %-20s FAIL\n" "no_dup_defs"; fail=1; }
$PY tools/eval/gate_store_coverage.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "store_coverage" || { printf "  %-20s FAIL\n" "store_coverage"; fail=1; }
$PY tools/eval/gate_ask_quantity.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "ask_quantity" || { printf "  %-20s FAIL\n" "ask_quantity"; fail=1; }
$PY tools/eval/distribution_gate.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "distribution_gate" || { printf "  %-20s FAIL\n" "distribution_gate"; fail=1; }
# The generator may only emit relations the CLEANED store contains. docs/RESULT_STORE_CLEANING.md
# deleted 34 records for documented reasons and units_train.json was never re-cleaned; a change that
# made units_train the iterated set silently re-admitted 24 of them, three named "Strategy".
$PY tools/eval/gate_store_authority.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "store_authority" || { printf "  %-20s FAIL\n" "store_authority"; fail=1; }
# The record span the generator writes must match the one the device assembles. Three producers of
# one format disagreed: 0 of 197,428 training documents carried the LHS unit that assemble.c emits
# on every prompt. EXPERIMENT_PLAN records this skew as fixed -- the five-field skeleton was
# unified, the units field was not, and nothing compared them afterwards.
$PY tools/eval/gate_format_parity.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "format_parity" || { printf "  %-20s FAIL\n" "format_parity"; fail=1; }
# THE LOSS MASK -- TOOL_SPEC s1, "the one rule that matters most". It was an inline loop copied into
# eight trainers with no function, no test and no gate, and every copy leaked 38.7% of each result
# span into the loss: the model was trained to predict the leading digits of values it is supposed
# to READ. Also refuses a ninth copy.
$PY train/test_lossmask.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_lossmask" || { printf "  %-20s FAIL\n" "test_lossmask"; fail=1; }
# tools/eval/prov_mutation.py is deliberately NOT listed here. It rebuilds and re-runs THIS SCRIPT
# once per mutation, so listing it makes the suite call itself -- which is what happened on the
# first attempt: infinite recursion, killed at the two-minute timeout. It is a meta-check and runs
# manually, exactly as WIRING_AUDIT already classifies gate_mutation.py and positive_control.py.
# The same applies to shape_mutation.py, which does NOT re-enter this script and is therefore safe
# to list above.
./build/test_loader build/store.tns >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_loader" || { printf "  %-20s FAIL\n" "test_loader"; fail=1; }
./build/test_picker build/store.tns >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_picker" || { printf "  %-20s FAIL\n" "test_picker"; fail=1; }
./build/test_assemble build/store.tns >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_assemble" || { printf "  %-20s FAIL\n" "test_assemble"; fail=1; }
./build/test_tokenizer build/tok4096.tok build/tok_reference.json >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_tokenizer" || { printf "  %-20s FAIL\n" "test_tokenizer"; fail=1; }

# UI and interaction suites. These were written and NOT LISTED HERE, which is the same defect the
# gates exist to catch, pointed at the gates themselves: a suite nothing runs is a suite that does
# not exist. Every one of these is built from source that ships.
$PY tools/eval/test_ui_errs.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_ui_errs" || { printf "  %-20s FAIL\n" "test_ui_errs"; fail=1; }
# test_ckpt guards the LOADER. It is listed here and not only in the Makefile because this file,
# not TESTS, is what decides whether the suite passed -- a roster kept in two places drifts, and
# the half nobody reads is the half that silently stops running.
# test_ckpt exits 2 for CANNOT CHECK, which the loop below already renders distinctly from FAIL.
for b in test_search test_span test_exit test_toolrun test_chatstore test_bubble test_notation test_theme test_ckpt test_shapecheck test_prov test_select test_persist; do
    if [ ! -x "build/$b" ]; then
        # A MISSING binary is a failure, not a skip. "cannot check" and "checked and clean" must
        # never share an exit status.
        printf "  %-20s FAIL (not built)\n" "$b"; fail=1
    elif "./build/$b" >/dev/null 2>&1; then
        printf "  %-20s PASS\n" "$b"
    else
        printf "  %-20s FAIL\n" "$b"; fail=1
    fi
done

[ $fail -eq 0 ] && echo "  ALL GATES PASS" || echo "  GATE SUITE FAILED"
exit $fail
