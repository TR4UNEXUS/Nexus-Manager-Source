#!/bin/sh
# Build the pinned official SDK source into this project's dependency prefix.
set -eu
app=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
deps="$app/../dependencies"
source="$deps/libnx-source"
prefix="$deps/sdk/libnx"
commit=b156b4774fb2df7b93402169509266da7547fc4c
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITA64=${DEVKITA64:-$DEVKITPRO/devkitA64}
export PATH="$DEVKITA64/bin:$DEVKITPRO/tools/bin:$PATH"
if [ ! -d "$source" ]; then
  git clone --no-checkout https://github.com/switchbrew/libnx.git "$source"
  git -C "$source" checkout --detach "$commit"
fi
if [ -d "$source/.git" ]; then
  test "$(git -C "$source" rev-parse HEAD)" = "$commit"
  git -C "$source" diff --exit-code HEAD -- nx
else
  python3 "$app/tools/verify-vendored-libnx.py"
fi
make -C "$source" -j"${NEXUS_BUILD_JOBS:-2}"
mkdir -p "$prefix"
for entry in include lib default_icon.jpg switch_rules switch.ld switch.specs; do
  cp -a "$source/nx/$entry" "$prefix/"
done
cp -a "$source/nx/external/bsd/include/." "$prefix/include/"
cp "$source/LICENSE.md" "$prefix/LICENSE.md"
printf '%s\n' "$commit" > "$prefix/SOURCE-COMMIT.txt"
graphics="$deps/sdk/graphics"
mkdir -p "$graphics/include" "$graphics/lib"
cp "$DEVKITPRO/libnx/include/deko3d.h" "$DEVKITPRO/libnx/include/deko3d.hpp" "$graphics/include/"
cp "$DEVKITPRO/libnx/lib/libdeko3d.a" "$DEVKITPRO/libnx/lib/libdeko3dd.a" "$graphics/lib/"
