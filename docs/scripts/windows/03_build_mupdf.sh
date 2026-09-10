#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# Build MuPDF 1.28.0 as static libraries (.a) for Windows/MSYS2
#
# Uses USE_SYSTEM_LIBS=yes to link thirdparty deps from MSYS2 system
# packages (no submodule compilation). Tesseract and Leptonica are
# explicitly excluded (Hodeia provides its own copies).
#
# MuPDF with USE_SYSTEM_LIBS=yes does NOT produce libmupdf-third.a
# (because THIRD_SRC is empty). We reconstruct it by bundling the
# MSYS2 system static libraries.
#
# Output:
#   external/lib/windows/libmupdf.a
#   external/lib/windows/libmupdf-third.a
#   external/lib/windows/libmupdf-threads.a
#   external/include/mupdf/...
#
# Run inside MSYS2 CLANG-UCRT64 shell.
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HODEIA="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

# Source location (auto-detect, or override via $1)
SRC="${1:-/c/Dependencies}"
MUPDF_SRC="$SRC/mupdf-1.28.0"

EXT_LIB="$HODEIA/external/lib/windows"
EXT_INC="$HODEIA/external/include"

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo "=== Hodeia root : $HODEIA"
echo "=== MuPDF src     : $MUPDF_SRC"
echo "=== Jobs          : $JOBS"
echo ""

# ─── Verify source ───────────────────────────────────────────
if [ ! -d "$MUPDF_SRC" ]; then
  echo "ERROR: MuPDF source not found at $MUPDF_SRC"
  echo "  Hint: pass the source root as \$1"
  exit 1
fi

# ─── Verify system libs ──────────────────────────────────────
echo "━━━ Checking MSYS2 system libs ━━━━━━━━━━━━━━━━━━━━━━━━━━━"

MISSING=0
for pkg in freetype2 harfbuzz libjpeg libopenjp2 zlib libbrotlidec libbrotlienc lcms2; do
  if pkg-config --exists "$pkg" 2>/dev/null; then
    echo "  ✓ $pkg"
  else
    echo "  ✗ $pkg NOT FOUND"
    MISSING=1
  fi
done

if [ "$MISSING" -eq 1 ]; then
  echo ""
  echo "ERROR: Missing system libraries. Run 00_install_packages.sh first."
  exit 1
fi

# ─── Generate embedded font resources ────────────────────────
echo ""
echo "━━━ Generating embedded font resources ━━━━━━━━━━━━━━━━━━━━"

cd "$MUPDF_SRC"
make generate -j"$JOBS"
echo "  ✓ Generated resources"

# ─── Build ───────────────────────────────────────────────────
echo ""
echo "━━━ Building MuPDF (static, system thirdparty) ━━━━━━━━━━━━"

cd "$MUPDF_SRC"
rm -rf build/release

# USE_SYSTEM_LIBS=yes         → link from MSYS2 packages
# USE_SYSTEM_TESSERACT=no     → exclude tesseract
# USE_SYSTEM_LEPTONICA=no     → exclude leptonica
# HAVE_GLUT=no / HAVE_X11=no  → no GUI deps
make -j"$JOBS" \
  CC=clang \
  CXX=clang++ \
  AR="ar rcs" \
  RANLIB=ranlib \
  USE_SYSTEM_LIBS=yes \
  USE_SYSTEM_TESSERACT=no \
  USE_SYSTEM_LEPTONICA=no \
  HAVE_GLUT=no \
  HAVE_X11=no \
  HAVE_LIBCRYPTO= \
  libs

echo ""
echo "  ✓ MuPDF build done."

# ─── Verify outputs ──────────────────────────────────────────
MUPDF_OUT="$MUPDF_SRC/build/release"

for f in libmupdf.a libmupdf-threads.a; do
  if [ ! -f "$MUPDF_OUT/$f" ]; then
    echo "ERROR: $f not found at $MUPDF_OUT/"
    ls -1 "$MUPDF_OUT/"*.a 2>/dev/null || echo "  (no .a files found)"
    exit 1
  fi
done

# ─── Reconstruct libmupdf-third.a from system static libs ────
echo ""
echo "━━━ Creating libmupdf-third.a from system static libs ━━━━━━"

MSYS_LIBDIR="/clangucrt64/lib"

SYSTEM_STATIC_LIBS=()

for libname in \
  libfreetype.a \
  libharfbuzz.a \
  libjpeg.a \
  libopenjp2.a \
  libz.a \
  libbrotlidec.a \
  libbrotlienc.a \
  liblcms2.a; do

  if [ -f "$MSYS_LIBDIR/$libname" ]; then
    SYSTEM_STATIC_LIBS+=("$MSYS_LIBDIR/$libname")
    echo "  + $libname"
  elif [ -f "$MSYS_LIBDIR/${libname%.a}.dll.a" ]; then
    SYSTEM_STATIC_LIBS+=("$MSYS_LIBDIR/${libname%.a}.dll.a")
    echo "  + ${libname%.a}.dll.a (import lib)"
  else
    echo "  WARNING: $libname not found in $MSYS_LIBDIR"
  fi
done

if [ ${#SYSTEM_STATIC_LIBS[@]} -gt 0 ]; then
  ar rcs "$MUPDF_OUT/libmupdf-third.a" "${SYSTEM_STATIC_LIBS[@]}"
  ranlib "$MUPDF_OUT/libmupdf-third.a"
  echo ""
  echo "  ✓ libmupdf-third.a created (${#SYSTEM_STATIC_LIBS[@]} libs bundled)"
else
  echo "ERROR: No system static libs found to bundle."
  exit 1
fi

# ─── Install artefacts ───────────────────────────────────────
echo ""
echo "━━━ Installing artefacts ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

mkdir -p "$EXT_LIB"
cp "$MUPDF_OUT/libmupdf.a"           "$EXT_LIB/libmupdf.a"
cp "$MUPDF_OUT/libmupdf-third.a"     "$EXT_LIB/libmupdf-third.a"
cp "$MUPDF_OUT/libmupdf-threads.a"   "$EXT_LIB/libmupdf-threads.a"
echo "  ✓ MuPDF static libs → $EXT_LIB/"

mkdir -p "$EXT_INC/mupdf"
cp -r "$MUPDF_SRC/include/mupdf/"*   "$EXT_INC/mupdf/"
echo "  ✓ MuPDF headers → $EXT_INC/mupdf/"

echo ""
ls -lh "$EXT_LIB"/libmupdf*.a
echo ""
echo "Done."
