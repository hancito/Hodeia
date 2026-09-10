#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# Master build script — Hodeia Windows dependencies
#
# Builds the in-tree dependencies (llama.cpp + sqlite-vec) and
# verifies that the MSYS2 shared-DLL dependencies (MuPDF, Tesseract,
# Leptonica) are installed.
#
# On Windows, MuPDF / Tesseract / Leptonica are NOT built from source —
# they are consumed as shared DLLs from MSYS2 (see 00_install_packages.sh).
#
# Usage:
#   ./build_all_windows.sh [SOURCE_ROOT] [backends...]
#
#   SOURCE_ROOT : directory containing the llama.cpp source
#                 Default: /c/Dependencies
#
#   backends    : space-separated llama.cpp backends
#                 Options: cpu cuda vulkan  (default: cpu)
#
# Run inside MSYS2 UCRT64 shell.
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HODEIA="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

SRC="${1:-/c/Dependencies}"
shift || true
BACKENDS="$*"

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  Hodeia — Windows Dependency Build (MSYS2 UCRT64)           ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "  Hodeia root : $HODEIA"
echo "  Source root : $SRC"
echo "  Backends    : ${BACKENDS:-cpu cuda vulkan}"
echo ""
echo "  Build steps:"
echo "    1. Verify MSYS2 shared DLLs (MuPDF, Tesseract, Leptonica)"
echo "    2. llama.cpp          (shared DLLs, per backend)"
echo "    3. sqlite-vec         (shared DLL)"
echo ""
echo "  Artefacts go to: $HODEIA/external/lib/windows/"
echo ""

# ─── Step 1: Verify MSYS2 shared libraries ───────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " STEP 1/3 — Verify MSYS2 shared libraries"
echo "═══════════════════════════════════════════════════════════════"

MSYS_BIN=""
for p in /ucrt64/bin /mingw64/bin /clang64/bin; do
  if [[ -d "$p" ]]; then MSYS_BIN="$p"; break; fi
done

if [[ -z "$MSYS_BIN" ]]; then
  echo "  ERROR: No MSYS2 bin directory found."
  echo "  Run 00_install_packages.sh first."
  exit 1
fi

MISSING=0
for lib in mupdf tesseract leptonica; do
  # MSYS2 names: libmupdf.dll, libtesseract-5.5.dll, libleptonica-6.dll
  if ls "$MSYS_BIN/lib${lib}"*.dll 1>/dev/null 2>&1 || ls "$MSYS_BIN/${lib}"*.dll 1>/dev/null 2>&1; then
    echo "  [OK] $lib"
  else
    echo "  [MISS] $lib"
    MISSING=1
  fi
done

if [[ "$MISSING" -eq 1 ]]; then
  echo ""
  echo "  Missing MSYS2 packages. Install with:"
  echo "    pacman -S mingw-w64-ucrt-x86_64-mupdf \\"
  echo "              mingw-w64-ucrt-x86_64-tesseract-ocr \\"
  echo "              mingw-w64-ucrt-x86_64-leptonica"
  echo "  Or run: bash $SCRIPT_DIR/00_install_packages.sh"
  echo ""
  read -rp "Continue without PDF/OCR support? [y/N] " confirm
  [[ "$confirm" == [yY] ]] || exit 0
fi

# ─── Step 2: llama.cpp ──────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " STEP 2/3 — llama.cpp (${BACKENDS:-cpu})"
echo "═══════════════════════════════════════════════════════════════"
bash "$SCRIPT_DIR/04_build_llamacpp.sh" "$SRC" $BACKENDS

# ─── Step 3: sqlite-vec ─────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " STEP 3/3 — sqlite-vec"
echo "═══════════════════════════════════════════════════════════════"
bash "$SCRIPT_DIR/05_build_sqlitevec.sh"

# ─── Summary ─────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  Build complete!                                            ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "  In-tree artefacts:"
find "$HODEIA/external/lib/windows" -type f | sort | while read -r f; do
  echo "    $(ls -lh "$f" | awk '{print $5, $9}')"
done
echo ""
echo "  MSYS2 shared DLLs (at runtime, from $MSYS_BIN):"
for lib in mupdf tesseract leptonica; do
  ls "$MSYS_BIN/lib${lib}"*.dll 2>/dev/null | while read -r dll; do
    echo "    $(basename "$dll")"
  done
done
echo ""
echo "  Next step — build Hodeia:"
echo ""
echo "    mkdir -p $HODEIA/build && cd \$_"
echo "    cmake .. -G Ninja \\"
echo "      -DCMAKE_C_COMPILER=clang \\"
echo "      -DCMAKE_CXX_COMPILER=clang++ \\"
echo "      -DCMAKE_BUILD_TYPE=Release \\"
echo "      -DHODEIA_MINGW_PREFIX=C:/msys64/ucrt64 \\"
echo "      [-DHODEIA_QT_FROM_MSYS2=ON]"
echo "    cmake --build ."
echo ""
