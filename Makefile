# ChatTLM: the device application and its host test suite.
#
# WHY THIS FILE EXISTS. Until it did, build/chattlm.tns and every build/test_* binary existed only
# because someone had typed a fifteen-file cross-compile line into a shell. Nothing in the repo
# produced them -- which meant tools/eval/run_gates.sh ran ./build/test_* that nothing built, so a
# fresh clone failed the gates with "not built" and a typo in the link line was invisible until it
# was not.
#
#   make            host tests + the device binary
#   make tests      host only, no toolchain needed
#   make device     build/chattlm.tns
#   make check      build the tests, then run the full gate suite
#   make clean
#
# The device target needs the Ndless toolchain, which lives in the repo:
#   vendor/Ndless/ndless-sdk/{bin,toolchain/install/bin}
# tools/build_toolchain_macos.sh names the prefix if it is missing.

ROOT      := $(CURDIR)
SDK       := $(ROOT)/vendor/Ndless/ndless-sdk
DEVPATH   := $(SDK)/bin:$(SDK)/toolchain/install/bin:$(PATH)
BUILD     := build

CC        ?= cc
HOSTFLAGS := -O2 -DTLM_HOST -Isrc/store -Itools/eval
NSPFLAGS  := -O2 -Wall -std=gnu99 -marm -mcpu=arm926ej-s -Iinclude -Isrc/store -Isrc -Itools/eval

# ---- sources ---------------------------------------------------------------------------------
# The evaluator's CORE, verbatim from tools/eval/Makefile. Kept in step by hand; if that list
# changes this one must too, which is noted because a silent divergence would show up as a link
# error at best and a stale evaluator at worst.
EVAL_CORE := $(addprefix tools/eval/, fmt.c ast.c units.c parser.c numeric.c deriv.c solve.c \
                                      literal.c integrate.c stat.c dispatch.c)

APP_SRC   := src/store/app.c src/store/gfx.c src/store/loader.c src/store/assemble.c \
             src/store/tokenizer.c src/store/picker.c src/store/toolrun.c src/store/chatstore.c \
             src/store/shapecheck.c src/store/askparse.c src/store/pickui.c src/store/pointer.c

APP_HDR   := $(wildcard src/store/*.h)

DEV_SRC   := src/store/device_app.c $(APP_SRC) src/runq_nspire.c src/nspire.c include/nspire_screen.c

# app.c is #included by the suites that exercise its statics, so it is NOT linked separately there.
# app.c now draws the relation picker, so its state machine and the given parser link here
# too. Both are separate translation units precisely so they can be tested without app.c.
HOST_LINK := src/store/gfx.c src/store/chatstore.c src/store/pickui.c src/store/picker.c \
             src/store/askparse.c src/store/assemble.c src/store/loader.c $(BUILD)/hoststub.o

# ---- host tests ------------------------------------------------------------------------------
# Two groups, because they differ in what they link. INCLUDES_APP suites #include app.c directly to
# reach its file-scope state; the others link toolrun.c and the evaluator.
TESTS_APP  := test_search test_span test_exit test_bubble test_notation test_theme test_select test_persist test_palette test_autopick
TESTS_EVAL := test_toolrun test_prov
# test_prov.c has existed, correct and well-designed -- it even has the 'rounds to 2 sf is
# legitimate' case -- and was referenced by NO Makefile and NO gate. Its binary sat committed
# under tools/eval/ with nothing that rebuilt it. WIRING_AUDIT records provenance.c going from
# 'exists but is never called' to 'called but never verified'; this is the second half.
TESTS_PLAIN:= test_chatstore test_ckpt test_shapecheck test_ansmatch test_pointer
# STORE SUITES. These four ran as COMMITTED BINARIES that no rule rebuilt, so they could not see a
# source change: re-adding the pre-opened `<a>` bug to assemble.c and running `make check` gave ALL
# GATES PASS. run_gates.sh's own header claims `make check` 'BUILDS the host binaries first'; for
# four of its twenty-five gates that was false.
TESTS_STORE:= test_loader test_picker test_assemble test_tokenizer test_askparse test_pickui
TESTS      := $(TESTS_APP) $(TESTS_EVAL) $(TESTS_PLAIN) $(TESTS_STORE)

.PHONY: all tests device check clean
all: tests device

tests: $(addprefix $(BUILD)/,$(TESTS)) $(BUILD)/render_app tools/eval/shapecli tools/eval/provcli tools/eval/evalcli $(BUILD)/asmcli $(BUILD)/tlmui $(BUILD)/askcli $(BUILD)/pickcli $(BUILD)/rankcli $(BUILD)/keycost $(BUILD)/promptcheck $(BUILD)/devprompt

$(BUILD):
	@mkdir -p $(BUILD)

# The host has no wall clock to offer the UI and no model to generate from. Both stubs return the
# honest "nothing here" answer rather than a plausible default -- see app.h on app_clock_hour.
# DEPENDS ON THIS MAKEFILE, because its CONTENT lives here. With only an order-only
# prerequisite it was generated once and never again: adding app_store() to the stub left
# a stale hoststub.o on disk and every app.c suite failed to link against a symbol the
# rule already emitted. Existence is not freshness -- the same class as need_file vs
# need_fresh in run_gates.sh, one build system over.
$(BUILD)/hoststub.c: Makefile | $(BUILD)
	@printf '%s\n' \
	  '/* Host stubs. Generated by the Makefile; see app.h on why these return nothing. */' \
	  '#include "../src/store/loader.h"' \
	  'void app_request(const char *q, const char *r) { (void)q; (void)r; }' \
	  'int  app_clock_hour(void) { return -1; }' \
	  '/* The picker needs a REAL store or the family list is 13 empty rows, which is not the' \
	  ' * screen under review. Loaded once, lazily, from the shipped file; 0 when absent, which' \
	  ' * the picker draws as its own state rather than as an empty store. */' \
	  'static ns_store2 HS; static int HS_TRIED;' \
	  'const ns_store2 *app_store(void) {' \
	  '    if (!HS_TRIED) { HS_TRIED = 1; if (ns_load(&HS, "build/store.tns") != NS_OK) HS.n = 0; }' \
	  '    return HS.n ? &HS : 0;' \
	  '}' > $@

$(BUILD)/hoststub.o: $(BUILD)/hoststub.c
	$(CC) $(HOSTFLAGS) -c $< -o $@

# These suites #include src/store/app.c to reach its file-scope state, so app.c and app.h are real
# prerequisites. Without them a palette-only edit left build/test_theme "up to date" and the gate
# passed on a stale binary -- a suite that cannot see your change is worse than no suite.
$(addprefix $(BUILD)/,$(TESTS_APP)): $(BUILD)/%: tools/eval/%.c $(APP_SRC) src/store/app.h src/store/font_data.h $(BUILD)/hoststub.o | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< $(HOST_LINK) -lm

$(BUILD)/render_app: tools/eval/render_app.c $(APP_SRC) $(APP_HDR) $(BUILD)/hoststub.o | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< $(HOST_LINK) src/store/toolrun.c $(EVAL_CORE) -lm

$(BUILD)/test_prov: tools/eval/test_prov.c tools/eval/provenance.c | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< tools/eval/provenance.c -lm

$(BUILD)/test_toolrun: tools/eval/test_toolrun.c src/store/toolrun.c src/store/toolrun.h $(EVAL_CORE) | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/toolrun.c $(EVAL_CORE) -lm

# The touchpad gesture machine. It is a separate translation unit precisely so this can exist:
# the defect it was written for -- a physical click emitting no click event -- lived inside
# device_app.c, which only cross-compiles, so no host check could reach it.
$(BUILD)/test_pointer: tools/eval/test_pointer.c src/store/pointer.c src/store/pointer.h \
                       src/store/app.h | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< -lm

$(BUILD)/test_chatstore: tools/eval/test_chatstore.c src/store/chatstore.c src/store/chatstore.h | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/chatstore.c src/store/gfx.c -lm

# The structural call check (docs/ARCHITECTURE.md section 6). Built against the SHIPPED evaluator
# core, not a second expression grammar -- a validator with its own parser would disagree with the
# evaluator on exactly the inputs where disagreement matters.
$(BUILD)/test_shapecheck: tools/eval/test_shapecheck.c src/store/shapecheck.c src/store/shapecheck.h \
                          $(EVAL_CORE) | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/shapecheck.c $(EVAL_CORE) -lm

# The CLI both Python graders call. WIRING_AUDIT records that two separate graders exist, so a check
# added once covers half the surface; this is what lets both call the same C.
# asmcli is THE SHIPPED ASSEMBLER, invoked by train/e2e.py, train/remeasure.py and
# tools/eval/gate_format_parity.py -- every prompt those harnesses grade comes out of it. It was a
# committed binary with no rule, so this session's ns_assemble constant-inlining fix reached it only
# because it happened to be rebuilt by hand. A measurement harness whose prompt builder is a stale
# artefact is measuring last week's prompts.
$(BUILD)/asmcli: src/store/asmcli.c src/store/assemble.c src/store/loader.c src/store/assemble.h | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/assemble.c src/store/loader.c -lm

# provcli is what grade.py, select_run.py and score.py all shell out to -- the single most-called
# check in the repo -- and it was a COMMITTED BINARY that no rule rebuilt. A fix to provenance.c
# never reached it; every provenance number in this repo came from whatever was compiled at some
# past moment. Same defect as the four store suites and test_prov, on the check that matters most.
tools/eval/provcli: tools/eval/provcli.c tools/eval/provenance.c tools/eval/eval.h | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< tools/eval/provenance.c -lm

# evalcli is THE evaluator CLI -- corpus/generate.py executes every training call through it, and
# tools/eval/audit_dimensionless.py gates on it. It has a rule in tools/eval/Makefile, so
# gate_binaries correctly calls it buildable; what it did NOT have was a place in `make tests`, and
# gate_controls rebuilds with `make -sB tests`. So a control that mutated dispatch.c rebuilt
# everything EXCEPT the binary the gate runs, and SURVIVED -- reported as "the gate cannot fail"
# when the truth was "the fix never reached it".
#
# Same shape as provcli having no rule at all, one level out: it is not enough for an artefact to
# be buildable, the rebuild the harness actually runs has to reach it.
tools/eval/evalcli: $(EVAL_CORE) tools/eval/main.c tools/eval/eval.h
	$(CC) $(HOSTFLAGS) -o $@ tools/eval/main.c $(EVAL_CORE) -lm

tools/eval/shapecli: tools/eval/shapecli.c src/store/shapecheck.c src/store/shapecheck.h $(EVAL_CORE)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/shapecheck.c $(EVAL_CORE) -lm

$(BUILD)/test_loader:    tools/eval/../../src/store/test_loader.c    src/store/loader.c src/store/loader.h | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ src/store/test_loader.c src/store/loader.c -lm
$(BUILD)/test_picker:    src/store/test_picker.c    src/store/picker.c src/store/loader.c | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/picker.c src/store/loader.c -lm
$(BUILD)/test_assemble:  src/store/test_assemble.c  src/store/assemble.c src/store/loader.c | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/assemble.c src/store/loader.c -lm
$(BUILD)/test_tokenizer: src/store/test_tokenizer.c src/store/tokenizer.c | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/tokenizer.c -lm
# askparse.c is the record picker and the given parser, lifted out of app_request() in
# device_app.c precisely so it could be compiled here. build/askcli is the same code with a
# stdin driver, used to MEASURE retrieval rather than assert it.
# THE PREDICATE IS EXTRACTED FROM device_app.c AT BUILD TIME, never copied into the test.
# answer_states_result decides whether to print "[runtime result: ...]" over the model's prose, so
# a stale copy would test a function the calculator does not run. sed lifts the shipped one.
$(BUILD)/ansmatch_impl.h: src/store/device_app.c | $(BUILD)
	@sed -n '/^static int answer_states_result/,/^}/p' $< > $@
	@test -s $@ || { echo "  FATAL: answer_states_result not found in device_app.c"; exit 1; }
$(BUILD)/test_ansmatch: tools/eval/test_ansmatch.c $(BUILD)/ansmatch_impl.h | $(BUILD)
	$(CC) $(HOSTFLAGS) -I $(BUILD) -o $@ $< -lm

$(BUILD)/test_askparse:  tools/eval/test_askparse.c src/store/askparse.c src/store/picker.c src/store/assemble.c src/store/loader.c $(APP_HDR) | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/askparse.c src/store/picker.c src/store/assemble.c src/store/loader.c -lm
$(BUILD)/askcli:         tools/eval/askcli.c       src/store/askparse.c src/store/picker.c src/store/assemble.c src/store/loader.c $(APP_HDR) | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/askparse.c src/store/picker.c src/store/assemble.c src/store/loader.c -lm
# nomatchcli emits the RANKING SHAPE so a no-match rule is chosen by measurement, not by taste.
$(BUILD)/nomatchcli:     tools/eval/nomatchcli.c   src/store/askparse.c src/store/picker.c src/store/assemble.c src/store/loader.c $(APP_HDR) | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/askparse.c src/store/picker.c src/store/assemble.c src/store/loader.c -lm
# rankcli exists to MEASURE retrieval@k, with subject and control in one binary.
$(BUILD)/rankcli:        tools/eval/rankcli.c      src/store/askparse.c src/store/picker.c src/store/assemble.c src/store/loader.c $(APP_HDR) | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/askparse.c src/store/picker.c src/store/assemble.c src/store/loader.c -lm
# keycost measures what Suggested exists to move: keystrokes launch to answer. Both arms
# drive the shipped pk_key and assert arrival, so it counts navigation, not a model of it.
# promptcheck vets the empty screen's example prompts against BOTH ways they can fail: too wide
# for the pane (they were removed once for exactly that) and not found by the shortlist.
$(BUILD)/devprompt:      tools/eval/devprompt.c    src/store/pickui.c src/store/picker.c src/store/askparse.c src/store/assemble.c src/store/loader.c $(APP_HDR) | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/pickui.c src/store/picker.c src/store/askparse.c src/store/assemble.c src/store/loader.c -lm
$(BUILD)/promptcheck:    tools/eval/promptcheck.c  src/store/pickui.c src/store/picker.c src/store/askparse.c src/store/assemble.c src/store/loader.c src/store/gfx.c $(APP_HDR) | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/pickui.c src/store/picker.c src/store/askparse.c src/store/assemble.c src/store/loader.c src/store/gfx.c -lm
$(BUILD)/keycost:        tools/eval/keycost.c      src/store/pickui.c src/store/picker.c src/store/askparse.c src/store/assemble.c src/store/loader.c $(APP_HDR) | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/pickui.c src/store/picker.c src/store/askparse.c src/store/assemble.c src/store/loader.c -lm
# pickui.c is the picker's STATE MACHINE, split out of app.c so its navigation can be checked
# without a calculator. build/pickcli reports family coverage over the shipped store, which is
# the load-bearing claim: an unmapped record is unreachable by browsing, and under E that is
# unreachable at all.
$(BUILD)/test_pickui:    tools/eval/test_pickui.c  src/store/pickui.c src/store/picker.c src/store/askparse.c src/store/assemble.c src/store/loader.c $(APP_HDR) | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/pickui.c src/store/picker.c src/store/askparse.c src/store/assemble.c src/store/loader.c -lm
$(BUILD)/pickcli:        tools/eval/pickcli.c      src/store/picker.c src/store/loader.c $(APP_HDR) | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/picker.c src/store/loader.c -lm


# build/tlmui -- the host UI harness that tools/uiserver/server.py drives, and that every
# localhost UI decision was validated against. It was a COMMITTED BINARY with no rule, and the
# note in gate_binaries.py claimed NO SOURCE EXISTED for it. That was wrong, and wrong in the
# repo's own recurring way: absence of a build rule was read as absence of source. The source is
# right here -- tlm_demo.c has the main, ui_host.c implements ui.h against a terminal instead of
# nspireio, host_stubs.c replays the 24 device tokens at the measured 373 ms each.
#
# The set was recovered by SYMBOL DIFF against the committed artefact, not by guessing: this list
# is the unique one whose nm output the committed binary's is a subset of. It is a subset and not
# an equality -- the committed binary is missing ns_tok_special_id, which tokenizer.c exports
# today. So the artefact every UI claim rested on was already stale by at least one function.
#
# It links ui_host.c where the device links ui.c, and host_stubs.c where the device links
# runq_nspire.c. tlm_demo.c ITSELF is compiled unchanged, which is the whole claim: layout, column
# budget, truncation and scrolling are the shipped code. The MODEL is not -- see host_stubs.c.
$(BUILD)/tlmui: src/store/tlm_demo.c src/store/ui_host.c src/store/host_stubs.c \
                src/store/loader.c src/store/picker.c src/store/assemble.c \
                src/store/tokenizer.c src/store/ui.h | $(BUILD)
	$(CC) $(HOSTFLAGS) -o $@ $< src/store/ui_host.c src/store/host_stubs.c src/store/loader.c \
	      src/store/picker.c src/store/assemble.c src/store/tokenizer.c -lm

# Compiles runq_nspire.c on the HOST, which is the point: the loader that runs on the calculator is
# the one under test, not a reimplementation of its rules.
$(BUILD)/test_ckpt: tools/eval/test_ckpt.c src/runq_nspire.c | $(BUILD)
	$(CC) $(HOSTFLAGS) -DFIXED_GS=88 -o $@ $< -lm

# THE FORWARD-PASS GOLDEN. Compiles the same runq_nspire.c the calculator runs, so a claim that a
# hot-loop change is bit-exact is checkable on the host without a device round-trip. Phase 1 of the
# brief asks for this artefact; it did not exist until the soft-float attention work needed it.
$(BUILD)/golden_forward: tools/eval/golden_forward.c src/runq_nspire.c | $(BUILD)
	$(CC) $(HOSTFLAGS) -DFIXED_GS=88 -o $@ $< -lm


# ---- device ----------------------------------------------------------------------------------
device: $(BUILD)/chattlm.tns

$(BUILD)/chattlm.tns: $(DEV_SRC) $(EVAL_CORE) $(APP_HDR) | $(BUILD)
	@command -v nspire-gcc >/dev/null 2>&1 || export PATH="$(DEVPATH)"; \
	 PATH="$(DEVPATH)"; export PATH; \
	 nspire-gcc $(NSPFLAGS) -o $(BUILD)/chattlm.elf $(DEV_SRC) $(EVAL_CORE) -lm && \
	 genzehn --input $(BUILD)/chattlm.elf --output $(BUILD)/chattlm.zehn --name chattlm && \
	 make-prg $(BUILD)/chattlm.zehn $@ && \
	 rm -f $(BUILD)/chattlm.zehn && \
	 echo "  $@  $$(wc -c < $@ | tr -d ' ') bytes"

# ---- benches ---------------------------------------------------------------------------------
# WHY THESE LIVE HERE AND NOT IN bench/Makefile. bench/Makefile's BENCHES list names four programs
# and its pattern rule compiles exactly `$< + nspire_screen.c`. bench_rtc and bench_cas are absent
# from the list but at least buildable by that rule; bench_forward is NEITHER listed NOR buildable
# by it, because it links the inference engine. So nothing in the repo produced bench_forward.tns.
# It existed only because a multi-file cross-compile line had been typed into a shell once -- the
# exact situation the header of this file was written to end, reproduced in the bench tree.
#
# bench_forward additionally needs -DTLM_PROFILE. Without it `tlm_prof` and `tlm_prof_layers` do not
# exist (src/runq_nspire.c:505) and the link fails, which is the good case; the bad case is someone
# adding a stub to make the link succeed and then measuring a forward pass with no instrumentation.
BENCH_PLAIN := bench_platform bench_mem bench_mac bench_flash bench_rtc bench_cas
BENCHFLAGS  := -O2 -Wall -Wextra -std=gnu99 -marm -mcpu=arm926ej-s -Iinclude -Isrc -Isrc/store \
               -Ivendor/Ndless/ndless-sdk/thirdparty/nspire-io/include

.PHONY: bench
bench: $(addprefix bench/,$(addsuffix .tns,$(BENCH_PLAIN))) bench/bench_forward.tns

bench/%.elf: bench/%.c bench/common.h include/nspire_screen.c
	@PATH="$(DEVPATH)"; export PATH; \
	 nspire-gcc $(BENCHFLAGS) $< include/nspire_screen.c -o $@ -lnspireio

# The engine sources are real prerequisites: an edit to runq_nspire.c's profiling block must rebuild
# this, or the device runs an instrument that does not match the enum bench_forward.c declares.
bench/bench_forward.elf: bench/bench_forward.c bench/common.h src/runq_nspire.c src/nspire.c \
                         include/nspire_screen.c
	@PATH="$(DEVPATH)"; export PATH; \
	 nspire-gcc $(BENCHFLAGS) -DTLM_PROFILE -o $@ \
	   bench/bench_forward.c src/runq_nspire.c src/nspire.c include/nspire_screen.c -lnspireio -lm

bench/%.tns: bench/%.elf
	@PATH="$(DEVPATH)"; export PATH; \
	 genzehn --input $< --output $@.zehn --name $* && \
	 make-prg $@.zehn $@ && rm -f $@.zehn && \
	 echo "  $@  $$(wc -c < $@ | tr -d ' ') bytes"

# ---- gates -----------------------------------------------------------------------------------
# Builds first, so "not built" can never be mistaken for a passing suite.
check: tests
	@bash tools/eval/run_gates.sh

clean:
	rm -f tools/eval/shapecli tools/eval/provcli
	rm -f $(addprefix $(BUILD)/,$(TESTS)) $(BUILD)/render_app $(BUILD)/hoststub.[co] \
	      $(BUILD)/chattlm.elf $(BUILD)/chattlm.zehn
