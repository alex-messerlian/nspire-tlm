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
import atexit, os, pathlib, re, signal, subprocess, sys, time
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from mutatectx import mutating

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
    # The shipping number's framing. The control strips the lower-bound qualifier from the one
    # sentence that carries it, which is exactly how the figure would come loose in practice --
    # nobody deletes a citation, they paraphrase one.
    # The helper's timestamp guarantee. Breaking the mtime restore is the EXACT defect that
    # shipped four times; test_mutatectx must notice, or it is not testing the property.
    "test_mutatectx":  ("tools/eval/mutatectx.py",
                        "            os.utime(self.path, (self.mtime0, self.mtime0))",
                        "            pass  # mutation: drop the pristine-mtime restore"),
    # And the gate that keeps a fifth call site from being written.
    # POINTED AT THE PREDICATE, NOT THE BRANCH. The first mutation disabled the offender branch
    # and the control SURVIVED -- correctly, because a clean tree has no offenders, so a disabled
    # check and a check with nothing to find produce the same PASS. The gate now classifies two
    # built-in samples on every run, so breaking the predicate is observable.
    "mutate_helper":   ("tools/eval/gate_mutate_helper.py",
                        "                    if isinstance(t, ast.Name) and assigned.get(t.id, 0) == 1:",
                        "                    if False:"),
    # The exact shipped defect: a slot declared in the enum and never written by a PF_END, which
    # reads as 0 ticks and is indistinguishable from "this stage costs nothing".
    # Remove the shear precondition from one two-factor product record: the gate must notice that
    # the record now bounds neither its inputs' coupling nor its result. This is the EXACT defect
    # the n=600 read found -- a_t=r*alpha left open while its sibling v_t=r*omega was bounded.
    # A live subject: 'a' [m/s^2] is declared in 7 records with one window and is NOT exempted.
    # Two earlier attempts survived because they named the wrong subject -- dT appears in a single
    # record so it cannot conflict, and q is on the DELIBERATE list. A control must name the thing
    # the gate asserts about, not merely a string the gate's file contains.
    # A156, one control per clause, each aimed at the test_autopick case only that clause decides.
    "test_autopick":   ("src/store/askparse.c",     # A156 itself: "What is output work?" (no
                        "    if (givens_bind_uniquely(st, out[0], in, question)) return 1;",  # symbol, 66%)
                        "    if (0) return 1;"),
    "autopick_unique": ("src/store/askparse.c",     # series and parallel R_eqv: must stay ambiguous
                        "            && givens_bind(&st->rec[r], in)) return 0;",
                        "            && 0) return 0;"),
    # A157, one per clause: the tie-break itself, its ambiguity refusal, and a/A/I closing a clause.
    "autopick_tiebreak":("src/store/askparse.c",    # "Given m = 2, a = 3, find F" must pick F=m*a
                        "    if (asked_symbol_pick(st, in, question, &pick)) {",
                        "    if (0) {"),
    "autopick_ambig":  ("src/store/askparse.c",     # "find R and P" must be refused
                        "        if (named && strcmp(named, rec->formula)) return 0;",
                        "        if (0) return 0;"),
    "autopick_clause": ("src/store/askparse.c",     # "V, R, find I" must not pick P = V^2/R
                        "        return !(*f == 0 || *f == '?' || *f == '.' || *f == ',' || *f == ';' || *f == '!' || *f == ':');",
                        "        return 1;"),
    "autopick_other":  ("src/store/askparse.c",     # binds I = P/A, asks for v_d
                        "    return !names_other_variable(st, t, in, question);",
                        "    return 1;"),
    "autopick_letter": ("src/store/askparse.c",     # binds lambda, asks for K
                        "        if (n == 1 && single_letter_exempt(q, start, p)) continue;",
                        "        if (n == 1) continue;"),
    "autopick_units":  ("src/store/askparse.c",     # "d = 150 m, t = 12 s" must still bind v = d/t
                        "        return 1;                                               /* a unit: \"150 m\", \"m/s\" */",
                        "        return 0;"),
    # The two suites A156 found unrun, each given the control a wired suite owes.
    "test_askparse":   ("src/store/askparse.h",     # a bar of 0: the word problem and the
                        "#define ASK_CONFIDENT_MIN 75",  # out-of-scope question go confident
                        "#define ASK_CONFIDENT_MIN 0"),
    "test_ansmatch":   ("src/store/device_app.c",   # 2% -> 20%: "one zero short" would pass
                        "double tol = (want < 0 ? -want : want) * 0.02;",
                        "double tol = (want < 0 ? -want : want) * 2.0;"),
    # The wiring gate: drop a suite from the gate loop and it must notice.
    "tests_wired":     ("tools/eval/run_gates.sh",  # the suite's line stops running it
                        'then ./build/test_ansmatch >/dev/null', 'then : >/dev/null'),
    "decl_siblings":   ("corpus/generate.py",
                        '("F_net=m*a", "a"):', '("F_net=m*a", "a"): (7.0, 9.0, "x"),  #'),
    # D3: put back a document that declines an answerable question.
    # Break the OUT-OF-SCOPE DRAW, not the D3 rate. The old control mutated `nomatch = False` into
    # the enabled form; once D3 was legitimately restored that string became the correct state and
    # the control read as a live mutation in the tree. The invariant is not "D3 is off" -- it is
    # "a D3 question comes from outside the store", so the mutation makes it use the record-derived
    # question again, which is the exact defect the criterion measured at 100.0%.
    # Put back the defect that produced the retracted 16%: a split item supplying a value for a
    # constant the runtime inlines. Mutating the constant SET is the subject the gate asserts about.
    "split_valid":     ("corpus/build_splits.py",
                        "        _cv = _RUNTIME_CONSTANTS",
                        "        _cv = set()  # mutated"),
    # A47b. Put the record's canonical NAME back into the answer label. assemble.c never emits a
    # name, so this is a literal the model cannot read -- it was 13.5% of answered documents, and
    # the shipped model supplies the WRONG record's name 89.7% of the time on records it has not
    # memorised. The subject the question used is the thing being replaced, so this names the
    # subject gate_name_provenance asserts about.
    "name_provenance": ("corpus/generate.py",
                        '_nm = (d.get("subj") or d["name"]).lower()',
                        '_nm = d["name"].lower()  # mutated'),
    "d3_legitimacy":   ("corpus/generate.py",
                        "            q = rng.choice(_D3_STEMS)",
                        "            pass  # use the record-derived question"),
    "coupling_family": ("corpus/generate.py",
                        '"a_t=r*alpha":               (lambda v: v["alpha"] * v["r"]**2 <= 1.3e5,',
                        '"a_t=r*alpha_DISABLED":      (lambda v: v["alpha"] * v["r"]**2 <= 1.3e5,'),
    "prof_pairing":    ("src/runq_nspire.c",
                        "PF_END(PF_FFN);", "PF_END(PF_CLS);"),
    "shipping_number": ("docs/paper/FACTS.md",
                        "**3.00% [1.72, 5.17], n = 400**", "**3.00%**"),
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
    "items_refs":      ("tools/eval/items.json",  # put the stale reference back
                        '"A=F/p"', '"A=F/P"'),
    "dimensionless":   ("tools/eval/dispatch.c",   # revert F3: put the trailing "1" back
                        'if (!strcmp(args[1], "1")) {', 'if (0) {'),
    # Delete the control figure from the document that publishes the flattering one.
    # Revert A7: drop the constants back out of the question's givens.
    # Revert A8: give the mismatch branch its private RHS-only copy of the units rule back.
    # A gate that checks four fields needs a control per field, or it proves one. This reverts
    # A9 -- the condition field back to the name-derived lookup the device does not use.
    "format_parity_cond":("corpus/generate.py",
                        "    req = (_store.get(r.get(\"f\"), {}) or {}).get(\"req\") or r.get(\"req\")",
                        "    req = r.get(\"cond\")"),
    # A11 rewrote this gate: the control now reverts the FIT LABEL, which is the property it
    # checks today. Pairing a real record with fit:low is the shape assemble.c cannot emit.
    "fit_cue":         ("corpus/generate.py",
                        '        band = "high"          # A11: assemble.c:99',
                        '        band = "low" if mismatch else "high"    # A11'),
    "fit_cue_units":   ("corpus/generate.py",
                        "            umap = units_field(rec_r)",
                        "            umap = ' '.join(f\"{v}:{rec_r['units'][v]}\" for v in sorted("
                        "{x for x in VAR.findall(rec_r['f'].split('=',1)[1])}) "
                        "if v in rec_r.get('units',{}))"),
    "no_orphan_values":("corpus/generate.py",
                        "        shown  = free + consts", "        shown  = free"),
    # Revert the union: iterate the annotated set alone, and the 25 fall back out.
    "units_parity":    ("corpus/units_train.json",   # put the seconds back in one file only
                        '"T_h": "K"', '"T_h": "s"'),
    "no_collapse":     ("corpus/generate.py",   # accept a one-value pool window again
                        "    if len(pool) >= 5: return rng.choice(pool)",
                        "    if pool: return rng.choice(pool)"),
    "given_range":     ("corpus/generate.py",     # draw givens from the flat pool again
                        "            return sample_in_range(rng, rr[0], rr[1], rr[2]) if rr else sample_value(rng)",
                        "            return sample_value(rng)"),
    # A24: restore the MISDISPATCH -- give Snell's n_1 the angle window the positional heuristic
    # used to hand it, with the correct rule still present and unreachable.
    # A24: restore the MISDISPATCH -- give Snell's n_1 the angle window the positional
    # heuristic used to hand it, with the correct rule still present and unreachable.
    # A24: restore the MISDISPATCH -- give Snell's n_1 the angle window the positional
    # heuristic handed it, with the correct rule still present and unreachable. The line must
    # be the SNELL one: an earlier version matched h_i=... first and mutated a record whose
    # dispatch the gate does not assert, so the control SURVIVED.
    "given_range_kind":("corpus/generate.py",
                        '    ("theta_2=asin(((n_1*sin(theta_1))/(n_2)))", "n_1"):     "refractive_index",',
                        '    ("theta_2=asin(((n_1*sin(theta_1))/(n_2)))", "n_1"):     "angle_turn",'),
    # NO CONTROL ON A _RESULT_KIND DECLARATION, and the reason is worth keeping.
    #
    # gate_plausible and corpus/generate.py CALL THE SAME implausible(). Mutating a declaration
    # therefore disables the generator's DROP and the gate's CHECK in one step: the impossible
    # results enter the corpus and the gate, asking the same mutated question, cannot see them.
    # The control could never fire. It survived twice while I looked for the cause in sample size
    # and range width, and both of those turned out to be real problems too -- N=3000 gave 14
    # documents on the record under test, and (0,100) for g never fired on any real output.
    #
    # A CONTROL MUST BREAK THE PRODUCER OR THE CHECKER, NEVER THE DEFINITION THEY SHARE. The live
    # control below mutates the generator's DROP and leaves the gate's predicate intact.
    "plausible":       ("corpus/generate.py",    # stop dropping impossible results
                        "            if _why:", "            if False:"),
    # Revert the units ORDER to sorted -- the skew gate_format_parity excused as cosmetic.
    "res_verbatim":    ("corpus/generate.py",     # round <res> again, as it was
                        "        a_val = res\n", "        a_val = res\n        res = a_val = "
                        "f\"{float(res):.4g}\" if res.replace('.','').replace('-','')"
                        ".replace('e','').isdigit() else res\n"),
    "record_bytes":    ("corpus/generate.py",
                        "    seen, vs = set(), []", "    seen, vs = set(), sorted({v for v in "
                        "VAR.findall(r['f'].split('=',1)[1])} - {'pi','e'}); vs = list(vs); vs2 = []\n"
                        "    for _ in []: pass\n    _unused = []"),
    "no_dup_defs":     ("corpus/generate.py",   # duplicate a top-level definition
                        "def quantity_range(rec, var):",
                        "def quantity_range(rec, var):\n    pass\n\n\ndef quantity_range(rec, var):"),
    "store_coverage":  ("corpus/generate.py",
                        "_keys = list(_ann) + [f for f in _store if f not in _ann]",
                        "_keys = list(_ann)"),
    "ask_quantity":    ("corpus/generate.py",
                        "        ask  = ask_for(r, quantity_surface(r, rng), rng)",
                        "        ask  = rng.choice(ASK_ALL).format(q=quantity_surface(r, rng))"),
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
    # Re-pointed 2026-08-27: adding the A10 unit condition moved this line and the STALENESS
    # DETECTOR caught it -- which is the machinery working. A control whose target text has gone
    # is not a control, and it would have read as coverage forever.
    "test_score":      ("tools/eval/score.py",
                        '                 and r["prov_clean"] is True and r["shape"] != "mismatch"\n'
                        '                 and r["answer_unit_ok"] is not False)',
                        "                 )"),
    # And a control for the NEW condition specifically: dropping only the unit check must fail.
    # One control per condition, for the same reason format_parity needed one per field.
    "test_score_unit": ("tools/eval/score.py",
                        '                 and r["answer_unit_ok"] is not False)',
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
ALIAS = {"test_scope_wf": "test_scope", "test_scope_ref": "test_scope", "test_scope_rm": "test_scope",
         "format_parity_cond": "format_parity",
         "test_score_unit": "test_score",
         "fit_cue_units": "fit_cue",
         "given_range_kind": "given_range",
         "autopick_unique": "test_autopick", "autopick_other": "test_autopick",
         "autopick_letter": "test_autopick", "autopick_units": "test_autopick",
         "autopick_tiebreak": "test_autopick", "autopick_ambig": "test_autopick",
         "autopick_clause": "test_autopick",
         }   # one control per FIELD the parity gate checks


def run_gate(name):
    name = ALIAS.get(name, name)
    # Announce ourselves as the lock holder so run_gates.sh does not refuse OUR invocation --
    # it refuses everyone else, which is the point. See the guard at the top of that script.
    # RUN ONLY THE GATE UNDER TEST. This ran the WHOLE suite once per control -- 56 times -- and
    # the meta-gate took 26 minutes. I cut the wait short twice, and the second time a mutation was
    # still live when I committed: 0585c95 shipped the A6 revert with ALL GATES PASS printed just
    # above it, truthfully, about a different version of the file. The cost caused the defect.
    _env = dict(os.environ, TLM_CONTROLS_OWNER=str(os.getpid()),
                GATE_ONLY=ALIAS.get(name, name))
    r = subprocess.run(["bash", "tools/eval/run_gates.sh"], capture_output=True, text=True,
                       env=_env)
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

# FILE RESTORATION IS mutatectx's JOB, on every exit path including the interrupt one --
# importing it registers the atexit and signal hooks. Both of this file's restore paths and
# shape_mutation.py's carried the SAME defect independently (bytes back, mtime left at now),
# so the operation got a helper rather than a third fix. What stays here is the LOCK, which
# is this file's alone; the handler below exits through sys.exit() so mutatectx's atexit
# still fires and puts every mutated file back, bytes and timestamp.
for _sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
    try:
        signal.signal(_sig, lambda *_a: (_LOCK.exists() and _LOCK.unlink(),
                                         sys.exit(130)))   # -> mutatectx atexit restores files
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
        if find not in original:
            print(f"  STALE CONTROL  {name}: the text it mutates is gone from {path}")
            failures.append(name); continue
        # THE MUTATE-AND-RESTORE CONTRACT IS mutatectx's, in ONE place, for all three call sites.
        # Both of this file's paths -- per-control and interrupt -- carried the same defect
        # independently, and shape_mutation.py carried it a third time. The helper's rebuild hook
        # runs while the mtime is still stamped forward, so make sees the restore; the pristine
        # time goes back afterwards, so nothing downstream (least of all the cross-compiled device
        # binaries a host `make` never touches) looks stale.
        _rebuild = lambda: subprocess.run(["make", "-s", "tests"], capture_output=True)
        with mutating(f, rebuild=_rebuild) as _m:
            _m.write(original.replace(find, repl, 1))
            # THE MUTATE SIDE NEEDS THE STAMP TOO, and mutating.write() gives it. It was once left
            # plain while the restore side was fixed, so a mutation to tools/eval/dispatch.c did
            # NOT reach tools/eval/evalcli and the control reported SURVIVED -- which reads as
            # "this gate cannot fail" when the truth was "the mutation never got there".
            _rebuild()
            verdict = run_gate(name)
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
