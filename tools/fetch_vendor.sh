#!/usr/bin/env bash
# Clone the three upstream projects this repository builds against, at the commits it was built
# and tested with. They are not copied into git: each is a separate project under its own licence.
#
#   tools/fetch_vendor.sh              all three
#   tools/fetch_vendor.sh ndless       one of: ndless llama2c libnspire
#
#   vendor/Ndless     the Ndless SDK and loader (MPL 1.1). resources/Makefile applies
#                     resources/sdk-branding.patch to it; tools/build_toolchain_macos.sh builds the
#                     ARM cross compiler inside it.
#   vendor/llama2.c   Karpathy's llama2.c (MIT). The training and scoring code imports its model.py.
#   vendor/libnspire  the USB library behind tools/nspire-cli (LGPL 3). Build it with
#                     (cd vendor/libnspire && ./configure --prefix="$PWD/_install" && make install)
#
# An existing clone at the pinned commit is left alone; one at another commit is an error, not a
# silent checkout, because resources/Makefile refuses to patch any other Ndless revision.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

NDLESS_URL=https://github.com/ndless-nspire/Ndless.git
NDLESS_REV="$(cat "$ROOT/resources/VENDOR_COMMIT")"
LLAMA2C_URL=https://github.com/karpathy/llama2.c.git
LLAMA2C_REV=350e04fe35433e6d2941dce5a1f53308f87058eb
LIBNSPIRE_URL=https://github.com/lights0123/libnspire.git
LIBNSPIRE_REV=6515819cf34e71143363aeb7220cf7ba864e23de

fetch() {   # fetch <directory under vendor/> <url> <commit> [submodules]
    local dir="$ROOT/vendor/$1" url="$2" rev="$3" have
    if [ -d "$dir/.git" ]; then
        have="$(git -C "$dir" rev-parse HEAD)"
        if [ "$have" = "$rev" ]; then
            echo "  vendor/$1 already at ${rev:0:12}"
            return 0
        fi
        echo "  vendor/$1 is at ${have:0:12}, want ${rev:0:12}. Check it out by hand or remove it." >&2
        return 1
    fi
    mkdir -p "$ROOT/vendor"
    git clone --quiet "$url" "$dir"
    git -C "$dir" checkout --quiet "$rev"
    if [ "${4:-}" = submodules ]; then
        git -C "$dir" submodule update --quiet --init --recursive
    fi
    echo "  vendor/$1 at ${rev:0:12}"
}

want="${1:-all}"
case "$want" in
    all|ndless|llama2c|libnspire) ;;
    *) echo "usage: $0 [ndless|llama2c|libnspire]" >&2; exit 2 ;;
esac
[ "$want" = all ] || [ "$want" = ndless ]    && fetch Ndless    "$NDLESS_URL"    "$NDLESS_REV" submodules
[ "$want" = all ] || [ "$want" = llama2c ]   && fetch llama2.c  "$LLAMA2C_URL"   "$LLAMA2C_REV"
[ "$want" = all ] || [ "$want" = libnspire ] && fetch libnspire "$LIBNSPIRE_URL" "$LIBNSPIRE_REV"
exit 0
