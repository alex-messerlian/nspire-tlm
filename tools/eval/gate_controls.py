#!/usr/bin/env python3
"""EVERY GATE MUST HAVE A NEGATIVE CONTROL, and this is the gate that enforces it.

Three instances in one session of a check that could not fail: four store suites running committed
binaries no rule rebuilt, and a parity gate whose negative control PASSED with the fix reverted
because the gate carried its own copy of the rule. A gate with no registered control is a gate
nobody has proved can fail, and this repo's history says that is the default state, not the
exception -- 7 of 197 recorded diagnoses had a test that catches a revert.

CONTRACT. Every check listed in tools/eval/run_gates.sh must appear in CONTROLS below with a
mutation: a (file, find, replace) that reverts the behaviour the gate exists to protect. Running
this applies each mutation in isolation and requires that gate to FAIL. A gate with no entry is a
FAILURE here, not a skip -- that is the whole point, and it is why the roster is derived from
run_gates.sh rather than written by hand.

Slow by construction: it rebuilds per mutation. Run it manually and in CI, not inside run_gates.sh,
which would make the suite call itself. Meta-check, like gate_mutation.py and positive_control.py.

  python3 tools/eval/gate_controls.py            every gate
  python3 tools/eval/gate_controls.py NAME ...   just these
"""
import atexit, os, pathlib, re, signal, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
os.chdir(ROOT)

# name -> (file, find, replace) | None if the gate is genuinely uncontrollable, with a reason.
CONTROLS = {
    # The five corpus gates all read corpus/store_clean.json. Each control breaks the ONE property
    # that gate exists to check, in the store, so a gate that has stopped reading its input fails.
    "dim_gate":        ("corpus/store_clean.json",
                        '"U": "J"', '"U": "kg"'),
    "lhs_gate":        ("corpus/store_clean.json",
                        '"f": "U=m*g*h"', '"f": "m*g*h=U*1"'),
    "lint_leibniz":    ("corpus/store_clean.json",
                        '"f": "U=m*g*h"', '"f": "U=(d*V)/(d*t)"'),
    "lint_declaration":("corpus/store_clean.json",
                        '"f": "U=m*g*h"', '"f": "U=m*g*h*undeclared_var"'),
    "lint_fused_words":("corpus/store_clean.json",
                        '"name": "Hooke\'s law"', '"name": "Hooke\'s law inthe spring"'),
    "shape_spec":      ("tools/eval/shape_spec.py",
                        'if op not in ("Mult", "Add"):', 'if False:'),
    "shape_mutation":  ("src/store/shapecheck.c",
                        "if (nodecmp(cw, cg) == 0) return TLM_SHAPE_OK;", "if (1) return TLM_SHAPE_OK;"),
    "test_shapecheck": ("src/store/shapecheck.c",
                        "if (nodecmp(cw, cg) == 0) return TLM_SHAPE_OK;", "if (1) return TLM_SHAPE_OK;"),
    # asmcli, not provcli: `make` has a BUILT-IN `%: %.c` rule, and tools/eval/provcli.c sits beside
    # tools/eval/provcli, so removing the explicit rule still leaves an implicit one and the gate
    # (correctly) reports a rule exists. build/asmcli's source is src/store/asmcli.c, a different
    # directory, so no implicit rule reaches it -- which makes it the target that actually tests the
    # gate. The limitation is real and documented in gate_binaries.py: this gate detects NO RULE, not
    # "only an implicit rule that would build it wrong".
    "event_producers": ("src/store/device_app.c",
                        "e->kind = IN_CLICK;", "e->kind = IN_MOVE;"),
    "distribution_gate": ("tools/eval/distribution_gate.py",
                        'BASELINE = {"SELECT": 38.5', 'BASELINE = {"SELECT": 5.0'),
    # ---- the C host suites. Each control breaks the property IN THE CODE UNDER TEST, never in the
    # suite: a mutation to the assertions would prove only that the assertions run.
    # ---- the eight app.c suites. app.c is #included by each, so one file carries every control;
    # each breaks the ONE behaviour named in that suite's header comment.
    # A CALL SITE, not the helper. Mutating qbubble_textw's constant SURVIVED: the suite calls
    # qbubble_textw on BOTH sides of its comparison, so it asserts self-consistency and no edit to
    # the single definition can break it. The original defect was two CALL SITES disagreeing, which
    # is what this breaks -- and test_bubble now reads app.c to see it.
    "test_bubble":     ("src/store/app.c",        # the measure pass diverging from the draw pass
                        "total += gfx_text_wrap(0, 0, t->q, F_UI, C_INK, C_BUBBLE, qbubble_textw(pw), lh0, 0)",
                        "total += gfx_text_wrap(0, 0, t->q, F_UI, C_INK, C_BUBBLE, pw - 26, lh0, 0)"),
    "test_exit":       ("src/store/app.c",        # ESC-as-interrupt: the abort must be consumable
                        "int app_take_abort(void) { int a = ABORT; ABORT = 0; return a; }",
                        "int app_take_abort(void) { ABORT = 0; return 0; }"),
    "test_notation":   ("src/store/app.c",        # sub/superscript rewriting
                        "const char *g = (in[i] == '_' ? SUB : SUP)[in[i+1] - '0'];",
                        "const char *g = (in[i] == '_' ? SUP : SUB)[in[i+1] - '0'];"),
    "test_persist":    ("src/store/app.c",        # sessions are written at all
                        "if (chat_save(PERSIST, CHATS, NCHATS, CUR) != 0) toast(\"Could not save sessions\");",
                        "if (0) toast(\"Could not save sessions\");"),
    "test_search":     ("src/store/app.c",        # ranking: hits must sort by score, best first
                        "while (at > 0 && score[at - 1] < s) { score[at] = score[at - 1]; SHIT[at] = SHIT[at - 1]; at--; }",
                        "while (0) { score[at] = score[at - 1]; SHIT[at] = SHIT[at - 1]; at--; }"),
    "test_select":     ("src/store/app.c",        # the clipboard, which the device does not provide
                        "    snprintf(CLIP, sizeof CLIP, \"%s\", s);\n    return 1;",
                        "    CLIP[0] = 0;\n    return 1;"),
    # NOT span_disp: the first control mutated it and SURVIVED, because this suite drives
    # span_next directly. The tag-skip below IS the defect the suite's header describes.
    "test_span":       ("src/store/app.c",        # tag guts must never reach the transcript
                        "if (!*e || istag(e)) break;", "if (!*e) break;"),
    "test_theme":      ("src/store/app.c",        # the palette must actually swap
                        "const uint16_t *src = dark ? PAL_DARK : PAL_LIGHT;",
                        "const uint16_t *src = PAL_LIGHT;"),
    # Revert A6 to what shipped: substitute the asked-for quantity into ANY of the 44 mined
    # frames, including the 36 sentence fragments. ASK_ALL, not ASK -- reverting to the reviewed
    # list would not reintroduce the fragments and the control would prove nothing.
    "ask_quantity":    ("corpus/generate.py",
                        "        ask  = ask_for(r, quantity_surface(r, rng), rng)",
                        "        ask  = rng.choice(ASK_ALL).format(q=quantity_surface(r, rng))"),
    "test_ui_errs":    ("tools/webui/index.html",   # a code the evaluator emits, dropped from the map
                        '"!nosol":"no solution found",\n', ''),
    "test_loader":     ("src/store/loader.c",     # the truncation check: a short file must not pass
                        "if (!cnt || atoi(cnt) != declared || i != declared)", "if (0)"),
    # NOT ns_filter: the first control mutated its scope guard and SURVIVED, because this suite
    # browses families and never searches. The surviving control was the finding -- ns_filter's
    # scoping is covered by test_search, not here.
    "test_picker":     ("src/store/picker.c",     # family membership in ns_records_in_family
                        "for (int i = 0; i < st->n && n < max; i++) if (ns_family_of(st, i) == fam) out[n++] = i;",
                        "for (int i = 0; i < st->n && n < max; i++) out[n++] = i;"),
    "test_tokenizer":  ("src/store/tokenizer.c",
                        "int ns_tok_encode(const ns_tok *t, const char *text, int *out, int max) {",
                        "int ns_tok_encode(const ns_tok *t, const char *text, int *out, int max) { "
                        "if (t && text && out && max > 0) { out[0] = 0; return 1; }"),
    "test_chatstore":  ("src/store/chatstore.c",  # the path guard that makes \"wb\" safe
                        "if (!is_ours(path)) return -1;", "if (0) return -1;"),
    "test_ckpt":       ("src/runq_nspire.c",      # the size formula the loader validates against
                        "long long bytes = 256 + (2 * L * D + D) * 4;",
                        "long long bytes = 256 + (2 * L * D + D) * 4 + 8;"),
    "gate_binaries":   ("Makefile",
                        "$(BUILD)/asmcli: src/store/asmcli.c", "$(BUILD)/asmcli_DISABLED:"),
    "test_score":      ("tools/eval/score.py",
                        '                 and r["prov_clean"] is True and r["shape"] != "mismatch")',
                        "                 )"),
    "test_prov":       ("tools/eval/provenance.c",
                        "int prov_call_unsourced(const char *doc, double *first) {",
                        "int prov_call_unsourced(const char *doc, double *first) { (void)doc;(void)first; return 0; }\n"
                        "static int _dead(const char *doc, double *first) {"),
    "store_authority": ("corpus/generate.py",
                        '        _uncleaned.append((f, ann.get("name", "")))\n        continue',
                        "        pass"),
    "test_lossmask":   ("train/lossmask.py",
                        "            if tok == res_open:\n                inside = True\n                mask[b, c] = 1",
                        "            if tok == res_open:\n                inside = True"),
    "format_parity":   ("corpus/generate.py",
                        "    order = ([lhs] if lhs in u else []) + [v for v in vs if v != lhs]",
                        "    order = [v for v in vs if v != lhs]"),
    "test_assemble":   ("src/store/assemble.c",
                        'n = appends(out, cap, n, " | fit:high");',
                        'n = appends(out, cap, n, " | fit:high<a>");'),
    "test_toolrun":    ("src/store/toolrun.c",
                        'if (!res[0]) snprintf(res, sizeof res, "!give");',
                        'res[0] = 0;'),
    # ITEM 10: answer_ok has FIVE conditions and only two were controlled. Each of the other three
    # is mutated to a constant here -- the failure mode is not a subtle logic error, it is a check
    # quietly degrading to True, which is exactly what provenance did and what nothing noticed.
    "test_scope_wf":   ("tools/eval/grade.py",
                        "def well_formed(generation):\n    o = generation",
                        "def well_formed(generation):\n    return True\n    o = generation"),
    "test_scope_ref":  ("tools/eval/grade.py",
                        "    return bool(REF.search(generation))",
                        "    return False"),
    "test_scope_rm":   ("tools/eval/grade.py",
                        "def answer_matches_result(generation):\n    o = generation",
                        "def answer_matches_result(generation):\n    return True\n    o = generation"),
    "test_scope":      ("tools/eval/grade.py",
                        "            and answer_matches_result(generation) and prov_clean(prompt + generation)",
                        "            and answer_matches_result(generation)"),
    "test_genloop":    ("tools/eval/genloop.py",
                        "        lg[res_id] = -1e30", "        pass"),
}

# Gates with no control yet. Listed EXPLICITLY so the count is visible rather than absent.
UNCONTROLLED_REASON = {}

def gates_in_suite():
    txt = pathlib.Path("tools/eval/run_gates.sh").read_text()
    names = set(re.findall(r'printf "  %-20s PASS\\n" "([a-z0-9_]+)"', txt))
    names |= set(re.findall(r'printf "  %-20s PASS\\n" "\$g"', txt) and
                 re.findall(r'^for g in ([a-z0-9_ ]+); do', txt, re.M)[0].split() or [])
    m = re.search(r'^for b in ([a-z0-9_ ]+); do', txt, re.M)
    if m: names |= set(m.group(1).split())
    return sorted(names)

# Several controls exercise DIFFERENT clauses of ONE gate. The suffix names the clause; the gate
# whose verdict is read is the part before the first underscore-suffix in ALIAS.
ALIAS = {"test_scope_wf": "test_scope", "test_scope_ref": "test_scope", "test_scope_rm": "test_scope"}

def run_gate(name):
    name = ALIAS.get(name, name)
    r = subprocess.run(["bash", "tools/eval/run_gates.sh"], capture_output=True, text=True)
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] == name:
            return parts[1]
    return "ABSENT"

# A KILLED MUTATION RUN MUST NOT LEAVE THE TREE MUTATED. The `finally` below restores the source,
# and a two-minute timeout killed this script mid-mutation with genloop.py's <res> ban still
# replaced by `pass`. The gate suite caught it on the next run, which is the system working -- but a
# mutation harness that can leave the repo broken is a hazard of its own. Every touched file is
# registered here and restored by atexit AND by a signal handler, so SIGTERM and SIGINT unwind too.
# AND AN EXCLUSIVE LOCK, because the handlers above are defeated by CONCURRENCY. Two runs at once
# is not hypothetical -- it happened: run B read run A's ALREADY-MUTATED file as its "original",
# and when both were killed, B faithfully restored A's mutation. The tree was left holding
# `"U": "kg"` in store_clean.json and `return TLM_SHAPE_OK` in shapecheck.c, both of which read as
# ordinary edits in `git status`, and one `git add -A` away from being committed as the fix.
#
# Same shape as committing a mutated genloop.py: a mutation harness whose safety depends on
# unwinding cleanly needs to also guarantee that nothing else is unwinding at the same time.
_LOCK = pathlib.Path(__file__).resolve().parents[2] / ".gate_controls.lock"
try:
    _lock_fd = os.open(_LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    os.write(_lock_fd, str(os.getpid()).encode())
except FileExistsError:
    try:    _who = _LOCK.read_text().strip()
    except Exception: _who = "?"
    _alive = False
    try:    os.kill(int(_who), 0); _alive = True
    except Exception: pass
    if _alive:
        print(f"REFUSING TO RUN: another gate_controls is mutating this tree (pid {_who}).\n"
              f"  Two concurrent runs corrupt each other's saved originals and can leave a\n"
              f"  mutation committed. Wait for it, or remove {_LOCK} if that pid is gone.",
              file=sys.stderr)
        sys.exit(2)
    print(f"  stale lock from dead pid {_who}, taking it", file=sys.stderr)
    _lock_fd = os.open(_LOCK, os.O_CREAT | os.O_WRONLY | os.O_TRUNC)
    os.write(_lock_fd, str(os.getpid()).encode())
atexit.register(lambda: _LOCK.exists() and _LOCK.unlink())

_ORIGINALS = {}

def _restore_all():
    for path, text in list(_ORIGINALS.items()):
        try:
            if pathlib.Path(path).read_text() != text:
                pathlib.Path(path).write_text(text)
                print(f"  restored {path} (interrupted mid-mutation)", file=sys.stderr)
        except Exception:
            pass
    _ORIGINALS.clear()

atexit.register(_restore_all)
for _sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
    try:
        signal.signal(_sig, lambda *_a: (_restore_all(),
                                         _LOCK.exists() and _LOCK.unlink(),
                                         sys.exit(130)))
    except Exception:
        pass


def main():
    want = sys.argv[1:]
    roster = gates_in_suite() + [k for k in CONTROLS if k in ALIAS]
    missing = [g for g in roster if g not in CONTROLS and g not in UNCONTROLLED_REASON
               and g not in ALIAS.values() or (g in ALIAS.values() and g not in CONTROLS)]
    missing = sorted({g for g in roster if g not in CONTROLS and g not in UNCONTROLLED_REASON})
    print(f"gates in run_gates.sh: {len(roster)}   with a registered control: "
          f"{sum(1 for g in roster if CONTROLS.get(g))}   uncontrolled: {len(missing)}")
    if missing:
        print("  NO NEGATIVE CONTROL REGISTERED -- each of these is a gate nobody has proved can fail:")
        for g in missing: print(f"    {g}")

    subprocess.run(["make", "-s", "tests"], capture_output=True)
    failures = list(missing)
    for name in roster:
        spec = CONTROLS.get(name)
        if not spec or spec[1] is None:
            continue
        if want and name not in want:
            continue
        path, find, repl = spec
        f = pathlib.Path(path); original = f.read_text()
        _ORIGINALS[path] = original
        if find not in original:
            print(f"  STALE CONTROL  {name}: the text it mutates is gone from {path}")
            failures.append(name); continue
        try:
            f.write_text(original.replace(find, repl, 1))
            subprocess.run(["make", "-s", "tests"], capture_output=True)
            verdict = run_gate(name)
        finally:
            f.write_text(original)
            _ORIGINALS.pop(path, None)
            subprocess.run(["make", "-s", "tests"], capture_output=True)
        ok = verdict in ("FAIL", "CANNOT")
        print(f"  {'caught  ' if ok else 'SURVIVED'} {name:18} (reverted -> {verdict})")
        if not ok: failures.append(name)

    if failures:
        print(f"\n  {len(failures)} GATE(S) WITHOUT A PROVEN NEGATIVE CONTROL: {sorted(set(failures))}")
        return 1
    print("\n  every registered control fires")
    return 0

if __name__ == "__main__":
    sys.exit(main())
