#!/bin/sh
# Wrapper around Ndless's build_toolchain.sh for Apple Silicon / Homebrew.
#
# The upstream script's prerequisite check compiles `gcc -lgmp test.c`, which fails on macOS because
# Homebrew installs to /opt/homebrew and that is not on clang's default search path. zlib is
# additionally keg-only, so it needs its own -L/-I. Without these four exports the build stops at
# the prereq check with a misleading "GMP dependency seems to be missing".
set -eu
BREW=/opt/homebrew
export LIBRARY_PATH="${BREW}/lib:${BREW}/opt/zlib/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
export CPATH="${BREW}/include:${BREW}/opt/zlib/include${CPATH:+:$CPATH}"
export LDFLAGS="-L${BREW}/lib -L${BREW}/opt/zlib/lib"
export CPPFLAGS="-I${BREW}/include -I${BREW}/opt/zlib/include"
export PARALLEL="${PARALLEL:--j10}"

cd "$(dirname "$0")/../vendor/Ndless/ndless-sdk/toolchain"
echo "=== toolchain build start: $(date) ==="
echo "PARALLEL=${PARALLEL}  LIBRARY_PATH=${LIBRARY_PATH}"
./build_toolchain.sh
echo "=== toolchain build end: $(date) ==="
