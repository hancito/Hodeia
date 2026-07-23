#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# Build Leptonica 1.87.0 as static library (.a) for Windows/MSYS2
# Output: external/lib/windows/libleptonica.a + headers
#
# Run inside MSYS2 CLANG-UCRT64 shell.
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PAPERIZE="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

# Source location (auto-detect from known paths, or override via $1)
SRC="${1:-/c/Dependencies}"
LEPTO_SRC="$SRC/leptonica-1.87.0"
LEPTO_BUILD="$LEPTO_SRC/build"

EXT_LIB="$PAPERIZE/external/lib/windows"
EXT_INC="$PAPERIZE/external/include"

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "=== Paperize root : $PAPERIZE"
echo "=== Leptonica src : $LEPTO_SRC"
echo "=== Jobs          : $JOBS"
echo ""

# ─── Verify source ────────────────────────────────────────────
if [ ! -d "$LEPTO_SRC" ]; then
  echo "ERROR: Leptonica source not found at $LEPTO_SRC"
  echo "  Hint: pass the source root as \$1, or set src at /c/Dependencies"
  exit 1
fi

# ─── Build ────────────────────────────────────────────────────
echo "━━━ Building Leptonica (static) ━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

mkdir -p "$LEPTO_BUILD"
cd "$LEPTO_BUILD"

rm -f CMakeCache.txt

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_PROG=OFF \
  -DENABLE_PNG=ON \
  -DENABLE_JPEG=ON \
  -DENABLE_TIFF=ON \
  -DENABLE_ZLIB=ON \
  -DENABLE_GIF=OFF \
  -DENABLE_WEBP=OFF \
  -DENABLE_OPENJPEG=OFF

cmake --build . -- -j"$JOBS"

echo ""
echo "  ✓ Leptonica build done."

# ─── Verify no unresolved image lib symbols ──────────────────
echo ""
echo "━━━ Verifying static linkage ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
UNRESOLVED=$(nm "$LEPTO_BUILD/src/libleptonica.a" 2>/dev/null \
  | grep " U " \
  | grep -iE "png|jpeg|tiff|zlib" || true)

if [ -n "$UNRESOLVED" ]; then
  echo "  WARNING: Unresolved image symbols found:"
  echo "$UNRESOLVED" | head -5
  echo "  This means Leptonica expects dynamic PNG/JPEG/TIFF/ZLIB."
  echo "  The MuPDF thirdparty static libs should resolve these at final link."
else
  echo "  ✓ No unresolved image symbols — fully static."
fi

# ─── Install artefacts ───────────────────────────────────────
echo ""
echo "━━━ Installing artefacts ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

mkdir -p "$EXT_LIB"
cp "$LEPTO_BUILD/src/libleptonica.a" "$EXT_LIB/libleptonica.a"
echo "  ✓ libleptonica.a → $EXT_LIB/"

mkdir -p "$EXT_INC/leptonica"
cp "$LEPTO_SRC/src/"*.h "$EXT_INC/leptonica/"
if [ -f "$LEPTO_BUILD/src/config_auto.h" ]; then
  cp "$LEPTO_BUILD/src/config_auto.h" "$EXT_INC/leptonica/"
fi
echo "  ✓ leptonica headers → $EXT_INC/leptonica/"

echo ""
ls -lh "$EXT_LIB/libleptonica.a"
echo ""
echo "Done."
