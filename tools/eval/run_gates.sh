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

# RUN ONE GATE. gate_controls.py runs this suite ONCE PER CONTROL -- 56 of them -- so the whole
# suite ran 56 times and the meta-gate passed 26 minutes. That is not a performance note: I twice
# cut the wait short, and on the second occasion a control's mutation was still live when I
# committed, so commit 0585c95 shipped the A6 revert with ALL GATES PASS printed just above it.
# Cost is a correctness property (the project log); this makes the meta-gate affordable enough to wait for.
#
# GATE_ONLY names one gate; everything else is skipped. Unset, the suite behaves exactly as before.
_skip() { [ -n "${GATE_ONLY:-}" ] && [ "$1" != "$GATE_ONLY" ]; }

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
# A33. EXISTENCE IS NOT FRESHNESS, and the stale artefact here is the one the DEVICE reads.
# Correcting a wrong first law in store_clean.json left build/store.tns holding the old formula --
# need_file saw the path and passed, so every store-derived gate was checking a store nobody had
# rebuilt. Only gate_record_bytes caught it, and only because the diff happened to touch a record.
# Same class as gate_stale_figures over a corpus nobody regenerated, one level more dangerous:
# this build product is what ships.
need_fresh() {  # need_fresh <product> <source> <how to make it>
    if [ -e "$1" ] && [ -e "$2" ] && [ "$2" -nt "$1" ]; then
        printf "  %-20s STALE: older than %s -- run: %s\n" "$(basename "$1")" "$2" "$3"
        prereq_missing=1
    fi
}
need_file "$PY"              "python3 -m venv .venv-tok && .venv-tok/bin/pip install -r requirements.txt"
need_file "$STORE"           "the record store; pass a path as \$1 if it lives elsewhere"
need_file build/store.tns    "python3 tools/store_pack.py $STORE build/store.tns"
need_fresh build/store.tns "$STORE" "python3 tools/store_pack.py"
# THE EVAL SPLITS BAKE THE RECORD SPAN, so a store edit silently makes every arm serve a document
# the model was never trained on. Measured when the condition pass was scoped: 1,115 of 1,128 items
# across these ten files embed the literal "standard conditions", so editing a single `req` moves
# what the model TRAINS on and not one byte of what the arms SERVE.
#
# Until this line there was exactly ONE need_fresh in the suite, for build/store.tns. That is the
# same propagation class as units_train.json, build/store.tns, units_holdout.json and the
# non-physics records -- four instances already recorded -- and this is the instrument rather than
# the artefact, which is worse: a stale split does not fail, it reports a confident number about a
# corpus nobody trained on.
for _sp in corpus/split_fit.json corpus/split_fit_ho.json corpus/split_fit_m.json \
           corpus/split_d1.json corpus/split_answer_0.json corpus/split_answer_x.json \
           corpus/split_answer_s.json corpus/split_answer_w.json \
           corpus/split_select.json corpus/split_report.json; do
    need_fresh "$_sp" "$STORE" "rebuild the arm splits (fit_judgement.py / answer_control.py / d1_arm.py / build_splits.py)"
done
need_file build/tok4096.tok  "python3 tools/tok_pack.py"
need_file tools/eval/shapecli "make tests   # the structural call check, ARCHITECTURE.md s6"
if [ "$prereq_missing" -ne 0 ]; then
    echo "GATE SUITE DID NOT RUN -- prerequisites above are missing."
    echo "This is NOT a gate failure. Nothing was checked. Create them and re-run."
    exit 3
fi
for g in lhs_gate lint_leibniz lint_declaration lint_fused_words dim_gate; do
    if _skip "$g"; then continue; fi
    out=$($PY "tools/eval/$g.py" "$STORE" 2>&1); rc=$?
    case $rc in
      0) printf "  %-20s PASS\n" "$g" ;;
      2) printf "  %-20s CANNOT CHECK (counts as failure)\n" "$g"; fail=1 ;;
      *) printf "  %-20s FAIL (exit %d)\n" "$g" "$rc"; echo "$out" | head -4 | sed 's/^/      /'; fail=1 ;;
    esac
done
if ! _skip test_scope; then $PY tools/eval/test_scope.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_scope" || { printf "  %-20s FAIL\n" "test_scope"; fail=1; }; fi
# The structural-shape rule (docs/ARCHITECTURE.md s6) validated against all nine known traces,
# including the two that provenance and dim_gate are both documented as unable to see. Listed here
# because a rule nothing runs is a rule that will drift from the C that implements it.
if ! _skip shape_spec; then $PY tools/eval/shape_spec.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "shape_spec" || { printf "  %-20s FAIL\n" "shape_spec"; fail=1; }; fi
# The C implementation, and a mutation pass over it. All-green on a first run triggers the mutation
# pass, not confidence -- this repo has three recorded cases of a suite that passed everything while
# measuring nothing.
if ! _skip shape_mutation; then $PY tools/eval/shape_mutation.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "shape_mutation" || { printf "  %-20s FAIL\n" "shape_mutation"; fail=1; }; fi
# The project log names test_genloop.py as the EXECUTABLE guard that makes the Bug 5 ruling hold -- "the
# guard is executable and mutation-tested in both directions, which is what makes it hold". It was
# never listed here, so the guard that documentation could not provide was itself ungated.
if ! _skip test_genloop; then $PY tools/eval/test_genloop.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_genloop" || { printf "  %-20s FAIL\n" "test_genloop"; fail=1; }; fi
# score.py is the FOURTH grader -- it scores the 200-item eval set and was in no gate. It graded the
# executed result against the recorded reference and nothing else, so a call that invented its
# operands and landed on the right number passed. test_score also asserts that score.py and grade.py
# AGREE, because two graders means every check has to be added twice or it covers half the surface.
if ! _skip test_score; then $PY tools/eval/test_score.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_score" || { printf "  %-20s FAIL\n" "test_score"; fail=1; }; fi
# No tracked binary may exist without a rule that rebuilds it. provcli -- the check the
# architecture's central claim rests on -- was one, so a fix to provenance.c never reached the
# running binary. Third instance of the class after the four gate suites and build/asmcli.
if ! _skip gate_binaries; then $PY tools/eval/gate_binaries.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "gate_binaries" || { printf "  %-20s FAIL\n" "gate_binaries"; fail=1; }; fi
# ITEM 14: every event kind app.c HANDLES must have a producer. WIRING_AUDIT's own closing
# instruction, never carried out -- and IN_SCROLL is still handled and emitted by nothing.
if ! _skip event_producers; then $PY tools/eval/gate_event_producers.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "event_producers" || { printf "  %-20s FAIL\n" "event_producers"; fail=1; }; fi
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
if ! _skip items_refs; then $PY tools/eval/gate_items_refs.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "items_refs" || { printf "  %-20s FAIL\n" "items_refs"; fail=1; }; fi
# DIMENSIONLESS_AUDIT.md calls this "gating (exit 1 on any)". It was in no gate, and it ran
# `./evalcli` relative, so it only worked from tools/eval. Both fixed 2026-08-27.
if ! _skip dimensionless; then $PY tools/eval/audit_dimensionless.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "dimensionless" || { printf "  %-20s FAIL\n" "dimensionless"; fail=1; }; fi
# topic-scoping-artifact, promoted from UNENFORCED 2026-08-27: a published selection improvement
# must publish its same-size random control. Topic-scoping read 61.2% against a random-20 at 62.7%.
if ! _skip selection_control; then $PY tools/eval/gate_selection_control.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "selection_control" || { printf "  %-20s FAIL\n" "selection_control"; fail=1; }; fi
if ! _skip stale_figures; then $PY tools/eval/gate_stale_figures.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "stale_figures" || { printf "  %-20s FAIL\n" "stale_figures"; fail=1; }; fi
if ! _skip corpus_fresh; then $PY tools/eval/gate_corpus_fresh.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "corpus_fresh" || { printf "  %-20s FAIL\n" "corpus_fresh"; fail=1; }; fi
if ! _skip record_derivatives; then $PY tools/eval/audit_record_derivatives.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "record_derivatives" || { printf "  %-20s FAIL\n" "record_derivatives"; fail=1; }; fi
if ! _skip split_heldout; then $PY tools/eval/gate_split_heldout.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "split_heldout" || { printf "  %-20s FAIL\n" "split_heldout"; fail=1; }; fi
if ! _skip split_valid; then $PY tools/eval/gate_split_valid.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "split_valid" || { printf "  %-20s FAIL\n" "split_valid"; fail=1; }; fi
if ! _skip no_repo_symlink; then $PY tools/eval/gate_no_repo_symlink.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "no_repo_symlink" || { printf "  %-20s FAIL\n" "no_repo_symlink"; fail=1; }; fi
if ! _skip name_provenance; then $PY tools/eval/gate_name_provenance.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "name_provenance" || { printf "  %-20s FAIL\n" "name_provenance"; fail=1; }; fi
if ! _skip d3_legitimacy; then $PY tools/eval/gate_d3_legitimacy.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "d3_legitimacy" || { printf "  %-20s FAIL\n" "d3_legitimacy"; fail=1; }; fi
if ! _skip knowledge; then $PY tools/eval/gate_knowledge.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "knowledge" || { printf "  %-20s FAIL\n" "knowledge"; fail=1; }; fi
if ! _skip keypad; then $PY tools/eval/gate_keypad.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "keypad" || { printf "  %-20s FAIL\n" "keypad"; fail=1; }; fi
if ! _skip explanations; then $PY tools/eval/gate_explanations.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "explanations" || { printf "  %-20s FAIL\n" "explanations"; fail=1; }; fi
if ! _skip grade_relation; then $PY tools/eval/test_grade_relation.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "grade_relation" || { printf "  %-20s FAIL\n" "grade_relation"; fail=1; }; fi
if ! _skip no_dead_api; then $PY tools/eval/gate_no_dead_api.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "no_dead_api" || { printf "  %-20s FAIL\n" "no_dead_api"; fail=1; }; fi
if ! _skip decl_siblings; then $PY tools/eval/gate_declaration_siblings.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "decl_siblings" || { printf "  %-20s FAIL\n" "decl_siblings"; fail=1; }; fi
if ! _skip ascii_boundary; then $PY tools/eval/gate_ascii_boundary.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "ascii_boundary" || { printf "  %-20s FAIL\n" "ascii_boundary"; fail=1; }; fi
if ! _skip coupling_family; then $PY tools/eval/gate_coupling_family.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "coupling_family" || { printf "  %-20s FAIL\n" "coupling_family"; fail=1; }; fi
if ! _skip prof_pairing; then $PY tools/eval/gate_prof_pairing.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "prof_pairing" || { printf "  %-20s FAIL\n" "prof_pairing"; fail=1; }; fi
if ! _skip shipping_number; then $PY tools/eval/gate_shipping_number.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "shipping_number" || { printf "  %-20s FAIL\n" "shipping_number"; fail=1; }; fi
if ! _skip mutate_helper; then $PY tools/eval/gate_mutate_helper.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "mutate_helper" || { printf "  %-20s FAIL\n" "mutate_helper"; fail=1; }; fi
if ! _skip test_mutatectx; then $PY tools/eval/test_mutatectx.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_mutatectx" || { printf "  %-20s FAIL\n" "test_mutatectx"; fail=1; }; fi
# A7: 16.2% of documents used a constant in the CALL that appeared in neither the question nor
# the record -- recalled, not read -- and while it was absent the graders could not tell a
# correct constant from a fabricated one (both "unchecked").
# A8: the D2 branch kept its own copy of the units-field rule, so fit:low was 100% predictable
# from a one-bit formatting cue and every D2 refusal metric measured the cue.
if ! _skip fit_cue; then $PY tools/eval/gate_fit_cue.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "fit_cue" || { printf "  %-20s FAIL\n" "fit_cue"; fail=1; }; fi
if ! _skip bare_pipe; then $PY tools/eval/gate_bare_pipe.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "bare_pipe" || { printf "  %-20s FAIL\n" "bare_pipe"; fail=1; }; fi
if ! _skip refusal_cue; then $PY tools/eval/gate_refusal_cue.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "refusal_cue" || { printf "  %-20s FAIL\n" "refusal_cue"; fail=1; }; fi
if ! _skip spare_given; then $PY tools/eval/gate_spare_given.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "spare_given" || { printf "  %-20s FAIL\n" "spare_given"; fail=1; }; fi
if ! _skip d1_withheld; then $PY tools/eval/gate_d1_withheld.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "d1_withheld" || { printf "  %-20s FAIL\n" "d1_withheld"; fail=1; }; fi
if ! _skip cval_parity; then $PY tools/eval/gate_cval_parity.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "cval_parity" || { printf "  %-20s FAIL\n" "cval_parity"; fail=1; }; fi
if ! _skip split_well_posed; then $PY tools/eval/gate_split_well_posed.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "split_well_posed" || { printf "  %-20s FAIL\n" "split_well_posed"; fail=1; }; fi
if ! _skip ckpt_tokenizer; then $PY tools/eval/gate_ckpt_tokenizer.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "ckpt_tokenizer" || { printf "  %-20s FAIL\n" "ckpt_tokenizer"; fail=1; }; fi
if ! _skip no_orphan_values; then $PY tools/eval/gate_no_orphan_values.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "no_orphan_values" || { printf "  %-20s FAIL\n" "no_orphan_values"; fail=1; }; fi
# R_train superseteq R_store: 25 of 166 records were retrievable and never trained, worth
# 12.2% vs 41.0% correct. The only property with a measured effect on correctness.
# store_clean and units_train both carry units; the generator prefers units_train, so a fix
# applied to only one silently does not propagate. The temperature-in-seconds fix did exactly that.
if ! _skip units_parity; then $PY tools/eval/gate_units_parity.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "units_parity" || { printf "  %-20s FAIL\n" "units_parity"; fail=1; }; fi
# Physically impossible RESULTS -- the class nothing owned, because distribution_gate cannot
# see digits and dim_gate finds -28.75 a dimensionally fine efficiency.
# A21: gate_plausible filters RESULTS; every GIVEN was unchecked, and 69% of trig documents fed
# an angle of several full turns. Range is per QUANTITY, not per unit.
# Seven declared givens were drawn as 0.001 in 100% of documents: the window contained exactly
# one pool value. A variable that never varies is invisible to every range check.
if ! _skip no_collapse; then $PY tools/eval/gate_no_collapse.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "no_collapse" || { printf "  %-20s FAIL\n" "no_collapse"; fail=1; }; fi
if ! _skip given_range; then $PY tools/eval/gate_given_range.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "given_range" || { printf "  %-20s FAIL\n" "given_range"; fail=1; }; fi
if ! _skip plausible; then $PY tools/eval/gate_plausible.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "plausible" || { printf "  %-20s FAIL\n" "plausible"; fail=1; }; fi
# THE SAME DEFECT WAS FOUND FOUR TIMES, ONE FIELD OVER EACH TIME (units, condition, fit,
# missing). This compares the whole record span BYTE FOR BYTE against build/asmcli, so it
# cannot be outflanked by a field nobody thought of.
# <res> is the architecture's central mechanism: toolrun.c writes the evaluator string
# verbatim at SIG_DIGITS 10, and the generator was rounding it to 4 s.f. -- 52.88% of docs.
if ! _skip res_verbatim; then $PY tools/eval/gate_res_verbatim.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "res_verbatim" || { printf "  %-20s FAIL\n" "res_verbatim"; fail=1; }; fi
if ! _skip record_bytes; then $PY tools/eval/gate_record_bytes.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "record_bytes" || { printf "  %-20s FAIL\n" "record_bytes"; fail=1; }; fi
# A bulk edit doubled corpus/generate.py (1,070 -> 1,565 lines) and EVERY functional check
# passed -- Python takes the later definition. Only the mutation meta-gate noticed.
# TWICE a control mutation has been COMMITTED. gate_controls locks against a second run and
# run_gates refuses to read mid-mutation -- neither stops `git add -A`. The suite passed on
# the clean file and the commit captured the mutated one, milliseconds apart.
if ! _skip no_mutation; then $PY tools/eval/gate_no_mutation.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "no_mutation" || { printf "  %-20s FAIL\n" "no_mutation"; fail=1; }; fi
if ! _skip no_dup_defs; then $PY tools/eval/gate_no_dup_defs.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "no_dup_defs" || { printf "  %-20s FAIL\n" "no_dup_defs"; fail=1; }; fi
if ! _skip store_coverage; then $PY tools/eval/gate_store_coverage.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "store_coverage" || { printf "  %-20s FAIL\n" "store_coverage"; fail=1; }; fi
if ! _skip ask_quantity; then $PY tools/eval/gate_ask_quantity.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "ask_quantity" || { printf "  %-20s FAIL\n" "ask_quantity"; fail=1; }; fi
if ! _skip distribution_gate; then $PY tools/eval/distribution_gate.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "distribution_gate" || { printf "  %-20s FAIL\n" "distribution_gate"; fail=1; }; fi
# The generator may only emit relations the CLEANED store contains. docs/RESULT_STORE_CLEANING.md
# deleted 34 records for documented reasons and units_train.json was never re-cleaned; a change that
# made units_train the iterated set silently re-admitted 24 of them, three named "Strategy".
if ! _skip store_authority; then $PY tools/eval/gate_store_authority.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "store_authority" || { printf "  %-20s FAIL\n" "store_authority"; fail=1; }; fi
# The record span the generator writes must match the one the device assembles. Three producers of
# one format disagreed: 0 of 197,428 training documents carried the LHS unit that assemble.c emits
# on every prompt. EXPERIMENT_PLAN records this skew as fixed -- the five-field skeleton was
# unified, the units field was not, and nothing compared them afterwards.
if ! _skip format_parity; then $PY tools/eval/gate_format_parity.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "format_parity" || { printf "  %-20s FAIL\n" "format_parity"; fail=1; }; fi
# THE LOSS MASK -- TOOL_SPEC s1, "the one rule that matters most". It was an inline loop copied into
# eight trainers with no function, no test and no gate, and every copy leaked 38.7% of each result
# span into the loss: the model was trained to predict the leading digits of values it is supposed
# to READ. Also refuses a ninth copy.
if ! _skip test_lossmask; then $PY train/test_lossmask.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_lossmask" || { printf "  %-20s FAIL\n" "test_lossmask"; fail=1; }; fi
# tools/eval/prov_mutation.py is deliberately NOT listed here. It rebuilds and re-runs THIS SCRIPT
# once per mutation, so listing it makes the suite call itself -- which is what happened on the
# first attempt: infinite recursion, killed at the two-minute timeout. It is a meta-check and runs
# manually, exactly as WIRING_AUDIT already classifies gate_mutation.py and positive_control.py.
# The same applies to shape_mutation.py, which does NOT re-enter this script and is therefore safe
# to list above.
if ! _skip test_loader; then ./build/test_loader build/store.tns >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_loader" || { printf "  %-20s FAIL\n" "test_loader"; fail=1; }; fi
if ! _skip test_picker; then ./build/test_picker build/store.tns >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_picker" || { printf "  %-20s FAIL\n" "test_picker"; fail=1; }; fi
if ! _skip test_assemble; then ./build/test_assemble build/store.tns >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_assemble" || { printf "  %-20s FAIL\n" "test_assemble"; fail=1; }; fi
if ! _skip test_tokenizer; then ./build/test_tokenizer build/tok4096.tok build/tok_reference.json >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_tokenizer" || { printf "  %-20s FAIL\n" "test_tokenizer"; fail=1; }; fi

# UI and interaction suites. These were written and NOT LISTED HERE, which is the same defect the
# gates exist to catch, pointed at the gates themselves: a suite nothing runs is a suite that does
# not exist. Every one of these is built from source that ships.
if ! _skip test_ui_errs; then $PY tools/eval/test_ui_errs.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_ui_errs" || { printf "  %-20s FAIL\n" "test_ui_errs"; fail=1; }; fi
# test_ckpt guards the LOADER. It is listed here and not only in the Makefile because this file,
# not TESTS, is what decides whether the suite passed -- a roster kept in two places drifts, and
# the half nobody reads is the half that silently stops running.
# test_ckpt exits 2 for CANNOT CHECK, which the loop below already renders distinctly from FAIL.
for b in test_search test_span test_exit test_toolrun test_chatstore test_bubble test_notation test_theme test_ckpt test_shapecheck test_prov test_select test_persist test_palette test_pointer; do
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
