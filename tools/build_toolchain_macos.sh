#!/bin/sh
# Wrapper around Ndless's build_toolchain.sh for Apple Silicon / Homebrew.
#
# The upstream script's prerequisite check compiles `gcc -lgmp test.c`, which fails on macOS because
# Homebrew installs to /opt/homebrew and that is not on clang's default search path. zlib is
# additionally keg-only, so it needs its own -L/-I.
#
# BUT: putting /opt/homebrew/include on CPATH wholesale breaks the GCC build. Homebrew's gettext
# ships libintl.h there, which redefines setlocale and collides with GCC's own declaration -- libcpp
# fails with "expected unqualified-id" in every translation unit. So we expose ONLY the four headers
# GCC actually needs, via a symlink farm, and libintl.h stays invisible.
set -eu
BREW=/opt/homebrew
export LIBRARY_PATH="${BREW}/lib:${BREW}/opt/zlib/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
INCFARM="${INCFARM:-/tmp/ndinc}"
mkdir -p "${INCFARM}"
# COPY, do not symlink. GCC's configure canonicalises the header location it finds and records it
# as GMPINC; a symlink resolves straight back to ${BREW}/include, which puts the whole Homebrew
# prefix -- and therefore gettext's libintl.h -- back on every compile line. Copies have no path to
# resolve to.
for h in gmp.h gmpxx.h mpfr.h mpf2mpfr.h mpc.h; do
    [ -f "${BREW}/include/$h" ] && cp -f "${BREW}/include/$h" "${INCFARM}/$h"
done
for h in zlib.h zconf.h; do
    [ -f "${BREW}/opt/zlib/include/$h" ] && cp -f "${BREW}/opt/zlib/include/$h" "${INCFARM}/$h"
done
export CPATH="${INCFARM}${CPATH:+:$CPATH}"
export CPPFLAGS="-I${INCFARM}"   # NOT ${BREW}/include -- see the libintl note above
export LDFLAGS="-L${BREW}/lib -L${BREW}/opt/zlib/lib"
export LIBFARM_NOTE="headers are copied, not linked -- see above"
export PARALLEL="${PARALLEL:--j10}"

cd "$(dirname "$0")/../vendor/Ndless/ndless-sdk/toolchain"
echo "=== toolchain build start: $(date) ==="
echo "PARALLEL=${PARALLEL}  LIBRARY_PATH=${LIBRARY_PATH}"
./build_toolchain.sh
echo "=== toolchain build end: $(date) ==="
