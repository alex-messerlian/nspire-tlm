# nsp -- Nspire USB transfer CLI, built on libnspire.
#
# libnspire is fetched by tools/fetch_vendor.sh and built into vendor/libnspire/_install (see README.md).
NSP := ../../vendor/libnspire/_install
CFLAGS := -O2 -std=c99 -Wall -Wextra -I$(NSP)/include/libnspire -I$(NSP)/include
LDFLAGS := -L$(NSP)/lib -lnspire

nsp: nsp.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f nsp
.PHONY: clean
