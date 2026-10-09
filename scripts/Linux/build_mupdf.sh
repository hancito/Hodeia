#!/bin/bash
cd /run/media/hancito/VAULT/Developpement/Dependencies

git config --global --add safe.directory /run/media/hancito/VAULT/Developpement/Dependencies/mupdf

MUPDF_DIR="./mupdf"
HODEIA="/home/hancito/LLM_Stuff/Hodeia"
EXT_LIB="$HODEIA/external/lib/linux"

# ── Clean previous build output ──
rm -rf "$MUPDF_DIR/build"
echo "🧹 Cleaned $MUPDF_DIR/build"

# ── Build libraries and tools ──
cd "$MUPDF_DIR"
git submodule update --init --depth 1
make libs tools HAVE_X11=no HAVE_GLFW=no HAVE_GLUT=no build=release -j$(nproc)

# ── Copy static libs to Hodeia/external ──
mkdir -p "$EXT_LIB"
cp build/release/libmupdf*.a "$EXT_LIB/"
echo "📦 Copied MuPDF libs → $EXT_LIB/"

# ── Copy headers (fitz.h is an umbrella header pulling in all of mupdf/fitz/) ──
EXT_INC_MUPDF="$HODEIA/external/include/mupdf"
rm -rf "$EXT_INC_MUPDF"
cp -r include/mupdf "$HODEIA/external/include/"
echo "📦 Copied MuPDF headers → $EXT_INC_MUPDF/"

echo ""
echo "✅ MuPDF build done. Libraries in: $EXT_LIB/"
