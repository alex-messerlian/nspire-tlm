# Backend 1 host build. No dependencies beyond libc and libm.
#
# -ffp-contract=off matters: fused multiply-add changes results in the last bits, and TOOL_SPEC.md
# section 5.1 requires host and device to agree byte for byte. The host is arm64 with FMA; the device
# has no FPU at all. Turning contraction off on the host removes one source of divergence.

CC      ?= cc
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter -ffp-contract=off
LDLIBS  := -lm

CORE := fmt.c ast.c units.c parser.c numeric.c deriv.c solve.c literal.c integrate.c stat.c dispatch.c

# Device build. Requires the Ndless SDK on PATH; see docs/PHASE0.md M1 and H8.

all: evalcli test_eval fuzz

evalcli: $(CORE) main.c eval.h
	$(CC) $(CFLAGS) -o $@ $(CORE) main.c $(LDLIBS)

test_eval: $(CORE) test_eval.c tests.inc eval.h
	$(CC) $(CFLAGS) -o $@ $(CORE) test_eval.c $(LDLIBS)

fuzz: $(CORE) fuzz.c eval.h
	$(CC) $(CFLAGS) -o $@ $(CORE) fuzz.c $(LDLIBS)

check: test_eval fuzz
	./test_eval
	./fuzz 200000

clean:
	rm -f evalcli test_eval fuzz eval_device.elf eval_device.tns eval_device.zehn golden.txt

.PHONY: all check clean

# Cross-compile the acceptance suite for the calculator. PASS CRITERION: byte-identical to host.
NSPIRE_CFLAGS := -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter -marm -mcpu=arm926ej-s

eval_device.tns: $(CORE) device_main.c tests.inc eval.h
	nspire-gcc $(NSPIRE_CFLAGS) -o eval_device.elf $(CORE) device_main.c -lm
	genzehn --input eval_device.elf --output eval_device.zehn --name eval_device
	make-prg eval_device.zehn $@
	rm -f eval_device.zehn

device: eval_device.tns

# Golden host output, for byte-comparison against the device log.
golden.txt: test_eval
	./test_eval > $@ 2>&1 || true
	@echo "compare the device's eval_device.txt.tns against this"
