#!/bin/bash
cd /run/media/hancito/VAULT/Developpement/Dependencies

# Configuration
HODEIA="/home/hancito/LLM_Stuff/Hodeia"
EXT_LIB="$HODEIA/external/lib/linux"
EXT_INC="$HODEIA/external/include"

DEPS_DIR="$(pwd)"
LEPTO_SRC="$DEPS_DIR/leptonica"
TESS_SRC="$DEPS_DIR/tesseract"
LEPTO_BUILD="$LEPTO_SRC/build"
TESS_BUILD="$TESS_SRC/build"

# ─── Verify sources exist ─────────────────────────────────────────
if [ ! -d "$LEPTO_SRC" ]; then
  echo "ERROR: Leptonica source not found at $LEPTO_SRC"
  exit 1
fi
if [ ! -d "$TESS_SRC" ]; then
  echo "ERROR: Tesseract source not found at $TESS_SRC"
  exit 1
fi

# ─── 1. Build Leptonica ───────────────────────────────────────────
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Step 1/4 : Building Leptonica (static)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

rm -rf "$LEPTO_BUILD"
mkdir -p "$LEPTO_BUILD"
cd "$LEPTO_BUILD"

cmake .. \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_PROG=OFF \
  -DENABLE_PNG=ON \
  -DENABLE_JPEG=ON \
  -DENABLE_TIFF=ON \
  -DENABLE_ZLIB=ON \
  -DENABLE_GIF=OFF \
  -DENABLE_WEBP=OFF \
  -DENABLE_OPENJPEG=OFF \
  -G Ninja 2>/dev/null || \
cmake .. \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_PROG=OFF \
  -DENABLE_PNG=ON \
  -DENABLE_JPEG=ON \
  -DENABLE_TIFF=ON \
  -DENABLE_ZLIB=ON \
  -DENABLE_GIF=OFF \
  -DENABLE_WEBP=OFF \
  -DENABLE_OPENJPEG=OFF

cmake --build . -- -j$(nproc)

echo ""
echo "  Leptonica build done."

# Patch LeptonicaConfig.cmake so Tesseract sees the build-tree includes
# (the config file hardcodes CMAKE_INSTALL_PREFIX by default).
sed -i \
  "s|set(Leptonica_INCLUDE_DIRS.*|set(Leptonica_INCLUDE_DIRS      \"${LEPTO_BUILD}/src;${LEPTO_SRC}/src\")|" \
  LeptonicaConfig.cmake
echo "  ✓ Patched LeptonicaConfig.cmake → build-tree includes"

# ─── 2. Build Tesseract ───────────────────────────────────────────
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Step 2/4 : Building Tesseract (static, LSTM-only)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

rm -rf "$TESS_BUILD"
mkdir -p "$TESS_BUILD"
cd "$TESS_BUILD"

# -DDISABLED_LEGACY_ENGINE=1 must be a CXX compile definition: Tesseract's
# CMake option only removes legacy source files but does NOT define the macro,
# so #ifndef DISABLED_LEGACY_ENGINE guards in baseapi.cpp stay open.
CXX_FLAGS="-DDISABLED_LEGACY_ENGINE=1"

cmake .. \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TRAINING_TOOLS=OFF \
  -DBUILD_TESTS=OFF \
  -DDISABLED_LEGACY_ENGINE=ON \
  -DDISABLE_TIFF=ON \
  -DDISABLE_ARCHIVE=ON \
  -DDISABLE_CURL=ON \
  -DGRAPHICS_DISABLED=ON \
  -DLeptonica_DIR="$LEPTO_BUILD" \
  -DCMAKE_CXX_FLAGS="$CXX_FLAGS" \
  -G Ninja 2>/dev/null || \
cmake .. \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TRAINING_TOOLS=OFF \
  -DBUILD_TESTS=OFF \
  -DDISABLED_LEGACY_ENGINE=ON \
  -DDISABLE_TIFF=ON \
  -DDISABLE_ARCHIVE=ON \
  -DDISABLE_CURL=ON \
  -DGRAPHICS_DISABLED=ON \
  -DLeptonica_DIR="$LEPTO_BUILD" \
  -DCMAKE_CXX_FLAGS="$CXX_FLAGS"

cmake --build . -- -j$(nproc)

echo ""
echo "  Tesseract build done."

# ─── 3. Copy libraries & headers into Hodeia/external/ ──────────
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Step 3/4 : Installing artefacts into external/"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# ── Leptonica ──
rm -f "$EXT_LIB/libleptonica.a"
cp "$LEPTO_BUILD/src/libleptonica.a" "$EXT_LIB/libleptonica.a"
echo "  ✓ libleptonica.a  → $EXT_LIB/"

rm -rf "$EXT_INC/leptonica"
mkdir -p "$EXT_INC/leptonica"
cp "$LEPTO_SRC/src/"*.h "$EXT_INC/leptonica/"
# Also copy the generated config_auto.h
if [ -f "$LEPTO_BUILD/src/config_auto.h" ]; then
  cp "$LEPTO_BUILD/src/config_auto.h" "$EXT_INC/leptonica/"
fi
echo "  ✓ leptonica headers → $EXT_INC/leptonica/"

# ── Tesseract ──
rm -f "$EXT_LIB/libtesseract.a"
cp "$TESS_BUILD/libtesseract.a" "$EXT_LIB/libtesseract.a"
echo "  ✓ libtesseract.a  → $EXT_LIB/"

rm -rf "$EXT_INC/tesseract"
mkdir -p "$EXT_INC/tesseract"
cp "$TESS_SRC/include/tesseract/"*.h "$EXT_INC/tesseract/"
echo "  ✓ tesseract headers → $EXT_INC/tesseract/"

# ─── 4. Download eng.traineddata ──────────────────────────────────
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Step 4/4 : Installing tessdata (eng.traineddata)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

TESSDATA_DIR="$HODEIA/Resources/tessdata"
mkdir -p "$TESSDATA_DIR"

ENG="$TESSDATA_DIR/eng.traineddata"
if [ ! -f "$ENG" ]; then
  echo "  Downloading eng.traineddata (~4 MB) ..."
  curl -fSL -o "$ENG" \
    "https://github.com/tesseract-ocr/tessdata_fast/raw/main/eng.traineddata"
  echo "  ✓ eng.traineddata → $TESSDATA_DIR/"
else
  echo "  ✓ eng.traineddata already present"
fi

# ─── Summary ──────────────────────────────────────────────────────
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  Done. Artefacts:"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
ls -lh "$EXT_LIB"/lib{tesseract,leptonica}.a 2>/dev/null
echo ""
ls -lh "$TESSDATA_DIR"/ 2>/dev/null
echo ""
