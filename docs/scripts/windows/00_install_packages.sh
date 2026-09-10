#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# MSYS2 ucrt64 — Install all required pacman packages
#
# Run inside the **MSYS2 UCRT64** shell.
#
# On Windows, MuPDF / Tesseract / Leptonica are consumed as shared
# DLLs from MSYS2 — they are NOT built from source.  Only llama.cpp
# and sqlite-vec are compiled locally (see 04/05 scripts).
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

# ─── Verify we are in the right shell ─────────────────────────
if [[ -z "${MSYSTEM:-}" || "$MSYSTEM" != "UCRT64" ]]; then
  echo "WARNING: Not running in MSYS2 UCRT64 shell (MSYSTEM='${MSYSTEM:-unset}')."
  echo "  Open 'MSYS2 UCRT64' from the Start Menu, then re-run this script."
  echo ""
  read -rp "Continue anyway? [y/N] " confirm
  [[ "$confirm" == [yY] ]] || exit 0
fi

# ─── Core build toolchain ─────────────────────────────────────
CORE=(
  mingw-w64-ucrt-x86_64-clang
  mingw-w64-ucrt-x86_64-cmake
  mingw-w64-ucrt-x86_64-ninja
  mingw-w64-ucrt-x86_64-llvm-openmp
  mingw-w64-ucrt-x86_64-pkg-config
)

# ─── PDF / OCR (shared DLLs — linked by Hodeia at build time) ─
PDF_OCR=(
  mingw-w64-ucrt-x86_64-mupdf
  mingw-w64-ucrt-x86_64-tesseract-ocr
  mingw-w64-ucrt-x86_64-leptonica
)

# ─── Utilities ────────────────────────────────────────────────
UTILS=(
  git
  wget
  curl
)

echo "══════════════════════════════════════════════════════════════"
echo "  Hodeia — MSYS2 UCRT64 dependencies"
echo "══════════════════════════════════════════════════════════════"
echo ""

# ─── Install ──────────────────────────────────────────────────
ALL=("${CORE[@]}" "${PDF_OCR[@]}" "${UTILS[@]}")
echo "Installing ${#ALL[@]} packages..."
pacman -S --needed --noconfirm "${ALL[@]}"

# ─── Optional: Qt6 from MSYS2 ─────────────────────────────────
echo ""
read -rp "Install Qt6 from MSYS2? (skips separate Qt Online Installer) [y/N] " qt_confirm
if [[ "$qt_confirm" == [yY] ]]; then
  QT=(
    mingw-w64-ucrt-x86_64-qt6-base
    mingw-w64-ucrt-x86_64-qt6-declarative
    mingw-w64-ucrt-x86_64-qt6-shadertools
  )
  echo "Installing ${#QT[@]} Qt6 packages..."
  pacman -S --needed --noconfirm "${QT[@]}"
else
  echo "  Skipped. Install Qt6 separately (Qt Online Installer) and pass"
  echo "  -DCMAKE_PREFIX_PATH=... to cmake at configure time."
fi

# ─── Environment check ────────────────────────────────────────
echo ""
echo "══════════════════════════════════════════════════════════════"
echo "  Environment check"
echo "══════════════════════════════════════════════════════════════"
echo "CC    : $(clang --version 2>/dev/null | head -1 || echo 'clang NOT FOUND')"
echo "CXX   : $(clang++ --version 2>/dev/null | head -1 || echo 'clang++ NOT FOUND')"
echo "CMake : $(cmake --version 2>/dev/null | head -1 || echo 'cmake NOT FOUND')"
echo "Ninja : $(ninja --version 2>/dev/null || echo 'ninja NOT FOUND')"
echo ""

# Verify MSYS2 runtime DLLs exist
# MSYS2 layout: import libs in /ucrt64/lib/, runtime DLLs in /ucrt64/bin/
MSYS_BIN=""
for p in /ucrt64/bin /mingw64/bin /clang64/bin; do
  if [[ -d "$p" ]]; then MSYS_BIN="$p"; break; fi
done
if [[ -n "$MSYS_BIN" ]]; then
  echo "  MSYS2 bin dir: $MSYS_BIN"
  for lib in mupdf tesseract leptonica; do
    if ls "$MSYS_BIN/lib${lib}"*.dll 1>/dev/null 2>&1 || ls "$MSYS_BIN/${lib}"*.dll 1>/dev/null 2>&1; then
      echo "  [OK] $lib"
    else
      pkg="mingw-w64-ucrt-x86_64-${lib}"
      [[ "$lib" == "tesseract" ]] && pkg="mingw-w64-ucrt-x86_64-tesseract-ocr"
      echo "  [MISS] $lib — run: pacman -S ${pkg}"
    fi
  done
else
  echo "  WARNING: No MSYS2 bin directory found."
fi

echo ""
echo "Done. Next steps:"
echo "  1. Build llama.cpp:    bash 04_build_llamacpp.sh [SRC_ROOT]"
echo "  2. Build sqlite-vec:   bash 05_build_sqlitevec.sh"
echo "  3. Configure Hodeia: see docs/BUILD_REQUIREMENTS.md"
