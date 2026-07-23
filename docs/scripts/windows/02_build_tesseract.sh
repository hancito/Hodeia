#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# Build Tesseract 5.5.2 as static library (.a) for Windows/MSYS2
# Depends on: Leptonica (build first with 01_build_leptonica.sh)
# Output: external/lib/windows/libtesseract.a + headers + tessdata
#
# Run inside MSYS2 CLANG-UCRT64 shell.
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PAPERIZE="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

# Source location (auto-detect, or override via $1)
SRC="${1:-/c/Dependencies}"
TESS_SRC="$SRC/tesseract-5.5.2"
TESS_BUILD="$TESS_SRC/build"

# Leptonica build directory (for -DLeptonica_DIR)
LEPTO_SRC="${2:-$SRC/leptonica-1.87.0}"
LEPTO_BUILD="$LEPTO_SRC/build"

EXT_LIB="$PAPERIZE/external/lib/windows"
EXT_INC="$PAPERIZE/external/include"

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "=== Paperize root  : $PAPERIZE"
echo "=== Tesseract src  : $TESS_SRC"
echo "=== Leptonica build: $LEPTO_BUILD"
echo "=== Jobs           : $JOBS"
echo ""

# ─── Verify sources ──────────────────────────────────────────
if [ ! -d "$TESS_SRC" ]; then
  echo "ERROR: Tesseract source not found at $TESS_SRC"
  exit 1
fi
if [ ! -f "$LEPTO_BUILD/LeptonicaConfig.cmake" ]; then
  echo "ERROR: Leptonica build not found at $LEPTO_BUILD"
  echo "  Run 01_build_leptonica.sh first."
  exit 1
fi

# ─── Build ───────────────────────────────────────────────────
echo "━━━ Building Tesseract (static, LSTM-only) ━━━━━━━━━━━━━━━━"

mkdir -p "$TESS_BUILD"
cd "$TESS_BUILD"

rm -f CMakeCache.txt

cmake .. -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TRAINING_TOOLS=OFF \
  -DBUILD_TESTS=OFF \
  -DDISABLED_LEGACY_ENGINE=ON \
  -DDISABLE_TIFF=ON \
  -DDISABLE_ARCHIVE=ON \
  -DDISABLE_CURL=ON \
  -DGRAPHICS_DISABLED=ON \
  -DLeptonica_DIR="$LEPTO_BUILD"

cmake --build . -- -j"$JOBS"

echo ""
echo "  ✓ Tesseract build done."

# ─── Install artefacts ───────────────────────────────────────
echo ""
echo "━━━ Installing artefacts ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

cp "$TESS_BUILD/libtesseract.a" "$EXT_LIB/libtesseract.a"
echo "  ✓ libtesseract.a → $EXT_LIB/"

mkdir -p "$EXT_INC/tesseract"
cp "$TESS_SRC/include/tesseract/"*.h "$EXT_INC/tesseract/"
echo "  ✓ tesseract headers → $EXT_INC/tesseract/"

echo ""
ls -lh "$EXT_LIB/libtesseract.a"
echo ""
echo "Done."
