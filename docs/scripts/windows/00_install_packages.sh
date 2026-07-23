#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# MSYS2 clang-ucrt64 — Install all required pacman packages
#
# Run inside the **MSYS2 CLANG-UCRT64** shell.
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

# ─── Core build toolchain ───
CORE=(
  mingw-w64-clang-ucrt64-clang
  mingw-w64-clang-ucrt64-clang-extra-tools
  mingw-w64-clang-ucrt64-cmake
  mingw-w64-clang-ucrt64-ninja
  mingw-w64-clang-ucrt64-llvm-openmp
  mingw-w64-clang-ucrt64-pkg-config
  mingw-w64-clang-ucrt64-openssl
)

# ─── Image / format libraries (Leptonica + Tesseract deps) ───
IMAGING=(
  mingw-w64-clang-ucrt64-zlib
  mingw-w64-clang-ucrt64-libpng
  mingw-w64-clang-ucrt64-libjpeg-turbo
  mingw-w64-clang-ucrt64-tiff
  mingw-w64-clang-ucrt64-libwebp
  mingw-w64-clang-ucrt64-openjpeg2
  mingw-w64-clang-ucrt64-giflib
)

# ─── Text / font rendering (MuPDF thirdparty fallback) ───
FONTS=(
  mingw-w64-clang-ucrt64-freetype
  mingw-w64-clang-ucrt64-harfbuzz
  mingw-w64-clang-ucrt64-lcms2
  mingw-w64-clang-ucrt64-brotli
)

# ─── Optional Tesseract deps ───
OPTIONAL=(
  mingw-w64-clang-ucrt64-libarchive
  mingw-w64-clang-ucrt64-curl
)

# ─── Utilities ───
UTILS=(
  git
  wget
  curl
  python
)

echo "=== Installing MSYS2 clang-ucrt64 dependencies ==="
echo ""

ALL=("${CORE[@]}" "${IMAGING[@]}" "${FONTS[@]}" "${OPTIONAL[@]}" "${UTILS[@]}")

pacman -S --needed --noconfirm "${ALL[@]}"

echo ""
echo "=== Checking environment ==="
echo "CC  : $(clang --version | head -1)"
echo "CXX : $(clang++ --version | head -1)"
echo "cmake: $(cmake --version | head -1)"
echo "ninja: $(ninja --version)"
echo ""
echo "Done. You can now run the dependency build scripts."
