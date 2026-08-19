# Backend 1 host build. No dependencies beyond libc and libm.
#
# -ffp-contract=off matters: fused multiply-add changes results in the last bits, and TOOL_SPEC.md
# section 5.1 requires host and device to agree byte for byte. The host is arm64 with FMA; the device
# has no FPU at all. Turning contraction off on the host removes one source of divergence.

CC      ?= cc
CFLAGS  ?= -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter -ffp-contract=off
LDLIBS  := -lm

CORE := fmt.c ast.c units.c parser.c numeric.c deriv.c solve.c integrate.c stat.c dispatch.c

all: evalcli test_eval

evalcli: $(CORE) main.c eval.h
	$(CC) $(CFLAGS) -o $@ $(CORE) main.c $(LDLIBS)

test_eval: $(CORE) test_eval.c eval.h
	$(CC) $(CFLAGS) -o $@ $(CORE) test_eval.c $(LDLIBS)

check: test_eval
	./test_eval

clean:
	rm -f evalcli test_eval

.PHONY: all check clean
