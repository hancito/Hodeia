#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# Master build script — Paperize Windows dependencies
#
# Builds all dependencies in the correct order, then copies
# artefacts into external/lib/windows/.
#
# Usage:
#   ./build_all_windows.sh [SOURCE_ROOT] [backends...]
#
#   SOURCE_ROOT : directory containing leptonica-1.87.0, tesseract-5.5.2,
#                 mupdf, and llamacpp subdirectories.
#                 Default: /c/Dependencies
#
#   backends    : space-separated list of llama.cpp backends to build.
#                 Options: cpu cuda vulkan  (default: cpu cuda vulkan)
#
# Run inside MSYS2 CLANG-UCRT64 shell.
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PAPERIZE="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

SRC="${1:-/c/Dependencies}"
shift || true
BACKENDS="$*"

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  Paperize — Windows Dependency Build (MSYS2 clang-ucrt64)  ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "Paperize root : $PAPERIZE"
echo "Source root   : $SRC"
echo "Backends      : ${BACKENDS:-cpu cuda vulkan}"
echo ""
echo "Build order:"
echo "  1. Leptonica    (static)"
echo "  2. Tesseract    (static, depends on Leptonica)"
echo "  3. MuPDF        (static, independent)"
echo "  4. llama.cpp    (shared DLLs, per backend)"
echo "  5. sqlite-vec   (shared DLL)"
echo ""
echo "Artefacts go to: $PAPERIZE/external/lib/windows/"
echo ""
read -rp "Continue? [y/N] " confirm
if [[ "$confirm" != [yY] ]]; then
  echo "Aborted."
  exit 0
fi

# ─── Step 1: Leptonica ──────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " STEP 1/5 — Leptonica"
echo "═══════════════════════════════════════════════════════════════"
bash "$SCRIPT_DIR/01_build_leptonica.sh" "$SRC"

# ─── Step 2: Tesseract ──────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " STEP 2/5 — Tesseract"
echo "═══════════════════════════════════════════════════════════════"
bash "$SCRIPT_DIR/02_build_tesseract.sh" "$SRC"

# ─── Step 3: MuPDF ──────────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " STEP 3/5 — MuPDF"
echo "═══════════════════════════════════════════════════════════════"
bash "$SCRIPT_DIR/03_build_mupdf.sh" "$SRC"

# ─── Step 4: llama.cpp ──────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " STEP 4/5 — llama.cpp (${BACKENDS:-cpu cuda vulkan})"
echo "═══════════════════════════════════════════════════════════════"
bash "$SCRIPT_DIR/04_build_llamacpp.sh" "$SRC" $BACKENDS

# ─── Step 5: sqlite-vec ─────────────────────────────────────
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " STEP 5/5 — sqlite-vec"
echo "═══════════════════════════════════════════════════════════════"
bash "$SCRIPT_DIR/05_build_sqlitevec.sh"

# ─── Summary ────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  Build complete!                                            ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""
echo "Artefacts:"
echo ""
find "$PAPERIZE/external/lib/windows" -type f | sort | while read -r f; do
  ls -lh "$f"
done
echo ""
echo "Next step: build Paperize itself."
echo ""
echo "  mkdir -p $PAPERIZE/build && cd \$_"
echo "  cmake .. -G Ninja \\"
echo "    -DCMAKE_C_COMPILER=clang \\"
echo "    -DCMAKE_CXX_COMPILER=clang++ \\"
echo "    -DCMAKE_BUILD_TYPE=Release \\"
echo "    -DCMAKE_PREFIX_PATH=<path-to-Qt6>"
echo "  cmake --build ."
echo ""
