# Phase 0 micro-benchmarks for TI-Nspire CX II.
#
# Requires the Ndless SDK on PATH (nspire-gcc, genzehn, make-prg). See docs/PHASE0.md.
#
# -O2 not -O3: -O3 on this toolchain has a habit of vectorising nothing and inlining everything,
# which makes the disassembly unreadable. We need to READ the disassembly to confirm the hot loops
# emit ldmia/smlabb, so readability is a real requirement here, not a preference.

BENCHES  := bench_platform bench_mem bench_mac bench_flash
CFLAGS   := -O2 -Wall -Wextra -std=gnu99 -marm -mcpu=arm926ej-s -ffreestanding -I../include -I../vendor/Ndless/ndless-sdk/thirdparty/nspire-io/include
LDFLAGS  :=

all: $(addsuffix .tns,$(BENCHES))

%.elf: %.c common.h
	nspire-gcc $(CFLAGS) $< ../include/nspire_screen.c -o $@ $(LDFLAGS) -lnspireio

%.tns: %.elf
	genzehn --input $< --output $@.zehn --name $*
	make-prg $@.zehn $@
	rm -f $@.zehn

# Disassemble the hot loops. Do this BEFORE trusting any MB/s or MMAC/s number:
# if stream_read_sum did not compile to ldmia, B4 is measuring the wrong thing.
disasm: bench_mem.elf bench_mac.elf
	arm-none-eabi-objdump -d bench_mem.elf > bench_mem.dis
	arm-none-eabi-objdump -d bench_mac.elf > bench_mac.dis
	@echo "--- stream_read_sum: expect ldmia ---"
	@sed -n '/<stream_read_sum>:/,/^$$/p' bench_mem.dis
	@echo "--- dot_int16_smla: expect smlabb/smlatt pairs ---"
	@sed -n '/<dot_int16_smla>:/,/^$$/p' bench_mac.dis
	@echo "--- soft-float calls in bench_mac (expect __aeabi_f* only inside dot_float) ---"
	@grep -n "__aeabi_" bench_mac.dis || echo "  none found"

clean:
	rm -f *.elf *.tns *.zehn *.dis

.PHONY: all clean disasm
