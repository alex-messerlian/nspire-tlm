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
[ $fail -eq 0 ] && echo "  ALL GATES PASS" || echo "  GATE SUITE FAILED"
exit $fail
