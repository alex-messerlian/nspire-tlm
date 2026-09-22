AS := nspire-as
LD := "$(shell (which arm-elf-ld arm-none-eabi-ld arm-linux-gnueabi-ld | head -1) 2>/dev/null)"
OBJCOPY := "$(shell (which arm-elf-objcopy arm-none-eabi-objcopy arm-linux-gnueabi-objcopy | head -1) 2>/dev/null)"

LFLAGS := -nostdlib
DISTDIR := ../build

all: $(DISTDIR)/ChatTLM_Setup.tns

%.o: %.S
	$(AS) $(GCCFLAGS) -c $< -o $@

%.bin: %.elf
	$(OBJCOPY) -O binary $^ $@

ndless_installer.elf: stage0.o
	$(LD) $(LFLAGS) $^ -o $@

installer_payload.lua: ndless_installer.bin
	../vendor/Ndless/ndless/src/tools/LuaBin/luabin ndless_installer.bin - installer noescape > $@

Problem1.xml: installer_payload.lua
	sed -f template.sed Problem1_template.xml | sed -f template.sed | sed -f template.sed > Problem1.xml

$(DISTDIR)/ChatTLM_Setup.tns: Problem1.xml
	mkdir -p $(DISTDIR)
	../vendor/Ndless/ndless-sdk/tools/luna/luna Problem1.xml $@

test: $(DISTDIR)/ChatTLM_Setup.tns
	firebird-send $^ ndless

clean:
	rm -f Problem1.xml installer_payload.lua *.elf *.o *.bin $(DISTDIR)/ndless_installer_4.5.5-6.2.0.tns

.PHONY: test clean
