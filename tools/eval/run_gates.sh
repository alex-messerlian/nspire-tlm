#!/usr/bin/env bash
# Full gate suite. Run after EVERY repair batch, not only after a review batch -- a repair gets
# the same scrutiny as a finding. Three records had their units invalidated by an LHS repair and
# stayed invisible for turns because nothing re-ran the gates afterwards.
#
# EXIT STATUS IS THE RESULT. 0 = all gates ran and passed. Any non-zero = stop.
# Exit 2 from a gate means CANNOT CHECK, which is not the same as clean and is also a failure here.
set -u
STORE="${1:-corpus/store_clean.json}"
PY=.venv-tok/bin/python
fail=0
for g in lhs_gate lint_leibniz lint_declaration lint_fused_words dim_gate; do
    out=$($PY "tools/eval/$g.py" "$STORE" 2>&1); rc=$?
    case $rc in
      0) printf "  %-20s PASS\n" "$g" ;;
      2) printf "  %-20s CANNOT CHECK (counts as failure)\n" "$g"; fail=1 ;;
      *) printf "  %-20s FAIL (exit %d)\n" "$g" "$rc"; echo "$out" | head -4 | sed 's/^/      /'; fail=1 ;;
    esac
done
$PY tools/eval/test_scope.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_scope" || { printf "  %-20s FAIL\n" "test_scope"; fail=1; }
./build/test_loader build/store.tns >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_loader" || { printf "  %-20s FAIL\n" "test_loader"; fail=1; }
./build/test_picker build/store.tns >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_picker" || { printf "  %-20s FAIL\n" "test_picker"; fail=1; }
./build/test_assemble build/store.tns >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_assemble" || { printf "  %-20s FAIL\n" "test_assemble"; fail=1; }
./build/test_tokenizer build/tok4096.tok build/tok_reference.json >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_tokenizer" || { printf "  %-20s FAIL\n" "test_tokenizer"; fail=1; }

# UI and interaction suites. These were written and NOT LISTED HERE, which is the same defect the
# gates exist to catch, pointed at the gates themselves: a suite nothing runs is a suite that does
# not exist. Every one of these is built from source that ships.
$PY tools/eval/test_ui_errs.py >/dev/null 2>&1 && printf "  %-20s PASS\n" "test_ui_errs" || { printf "  %-20s FAIL\n" "test_ui_errs"; fail=1; }
for b in test_search test_span test_exit test_toolrun test_chatstore test_bubble test_notation test_theme; do
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
