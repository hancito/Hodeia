#!/bin/bash
# build_deps.sh
# Builds every third-party dependency of Hodeia from source at the pinned
# versions, PORTABLE (no -march=native, every llama.cpp CPU variant), and
# stages them into external/ — the layout cmake/PlatformLinux.cmake expects:
#   external/lib/linux/{libleptonica,libtesseract,libmupdf*}.a
#   external/lib/linux/llamacpp/*.so*      (CPU variants + Vulkan, no CUDA)
#   external/lib/linux/sqlitevec/vec0.so
#   external/include/{leptonica,tesseract,mupdf,llamacpp,SqliteVec}/
#
# Used by the DEB/RPM CI (.github/workflows/linux-packages.yml); runs the same
# steps as build() in package/arch/PKGBUILD with HODEIA_PORTABLE=1.
#
# Usage:  scripts/Linux/build_deps.sh <work_dir> [hodeia_root]
#   work_dir     where the sources are cloned and built (created if missing)
#   hodeia_root  the Hodeia checkout (default: this script's repository)

set -euo pipefail

# Pinned versions — keep in sync with package/arch/PKGBUILD and
# scripts/Linux/get_depedencies.sh.
LLAMA_VER=v0.9.0
MUPDF_VER=1.28.6
TESS_VER=5.5.4
LEPTO_VER=1.87.0
SQLITEVEC_VER=v0.1.10-alpha.4

[ $# -ge 1 ] || { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
WORK="$(mkdir -p "$1" && cd "$1" && pwd)"
HODEIA="$(cd "${2:-$(dirname "$0")/../..}" && pwd)"
EXT_LIB="$HODEIA/external/lib/linux"
EXT_INC="$HODEIA/external/include"
JOBS="$(nproc)"

clone() {  # clone <url> <tag> <dir>
  [ -d "$WORK/$3" ] || git clone --depth 1 --branch "$2" "$1" "$WORK/$3"
}

clone https://github.com/DanBloomberg/leptonica.git   "$LEPTO_VER"     leptonica
clone https://github.com/tesseract-ocr/tesseract.git  "$TESS_VER"      tesseract
clone https://github.com/ArtifexSoftware/mupdf.git    "$MUPDF_VER"     mupdf
clone https://github.com/asg017/sqlite-vec.git        "$SQLITEVEC_VER" sqlite-vec
clone https://github.com/ggml-org/llama.cpp.git       "$LLAMA_VER"     llama.cpp

rm -rf "$EXT_LIB"
mkdir -p "$EXT_LIB"

# ── 1. Leptonica (static) ────────────────────────────────────────────────
echo ">> Building Leptonica"
LEPTO_SRC="$WORK/leptonica"
LEPTO_BUILD="$WORK/leptonica-build"
cmake -S "$LEPTO_SRC" -B "$LEPTO_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_PROG=OFF \
  -DENABLE_PNG=ON -DENABLE_JPEG=ON -DENABLE_TIFF=ON -DENABLE_ZLIB=ON \
  -DENABLE_GIF=OFF -DENABLE_WEBP=OFF -DENABLE_OPENJPEG=OFF
cmake --build "$LEPTO_BUILD" -j"$JOBS"
# Tesseract consumes Leptonica from its build tree.
sed -i \
  "s|set(Leptonica_INCLUDE_DIRS.*|set(Leptonica_INCLUDE_DIRS \"${LEPTO_BUILD}/src;${LEPTO_SRC}/src\")|" \
  "$LEPTO_BUILD/LeptonicaConfig.cmake"

install -Dm644 "$LEPTO_BUILD/src/libleptonica.a" "$EXT_LIB/libleptonica.a"
rm -rf "$EXT_INC/leptonica"
mkdir -p "$EXT_INC/leptonica"
cp "$LEPTO_SRC"/src/*.h "$EXT_INC/leptonica/"
[ -f "$LEPTO_BUILD/src/config_auto.h" ] && cp "$LEPTO_BUILD/src/config_auto.h" "$EXT_INC/leptonica/"

# ── 2. Tesseract (static, LSTM only) ─────────────────────────────────────
echo ">> Building Tesseract"
TESS_SRC="$WORK/tesseract"
TESS_BUILD="$WORK/tesseract-build"
# -DDISABLED_LEGACY_ENGINE=1 must also be a compile definition: the CMake
# option only drops the legacy sources, the #ifndef guards need the macro.
# -march=core2 (Tesseract's default is -march=native when supported):
# keep the library runnable on any x86-64 CPU.
cmake -S "$TESS_SRC" -B "$TESS_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TRAINING_TOOLS=OFF \
  -DBUILD_TESTS=OFF \
  -DDISABLED_LEGACY_ENGINE=ON \
  -DDISABLE_TIFF=ON -DDISABLE_ARCHIVE=ON -DDISABLE_CURL=ON \
  -DGRAPHICS_DISABLED=ON \
  -DENABLE_NATIVE=OFF \
  -DLeptonica_DIR="$LEPTO_BUILD" \
  -DCMAKE_CXX_FLAGS="-DDISABLED_LEGACY_ENGINE=1"
cmake --build "$TESS_BUILD" -j"$JOBS"

install -Dm644 "$TESS_BUILD/libtesseract.a" "$EXT_LIB/libtesseract.a"
rm -rf "$EXT_INC/tesseract"
mkdir -p "$EXT_INC/tesseract"
cp "$TESS_SRC"/include/tesseract/*.h "$EXT_INC/tesseract/"
cp "$TESS_BUILD"/include/tesseract/*.h "$EXT_INC/tesseract/" 2>/dev/null || true

# ── 3. MuPDF (static) ────────────────────────────────────────────────────
echo ">> Building MuPDF"
git -C "$WORK/mupdf" submodule update --init --depth 1
# libmupdf-threads.a is not part of `libs` (it normally comes with `tools`).
make -C "$WORK/mupdf" -j"$JOBS" libs libmupdf-threads build=release \
  HAVE_X11=no HAVE_GLFW=no HAVE_GLUT=no
cp "$WORK"/mupdf/build/release/libmupdf*.a "$EXT_LIB/"
rm -rf "$EXT_INC/mupdf"
cp -r "$WORK/mupdf/include/mupdf" "$EXT_INC/"

# ── 4. sqlite-vec (loadable extension) ───────────────────────────────────
echo ">> Building sqlite-vec"
make -C "$WORK/sqlite-vec" loadable
install -Dm755 "$WORK/sqlite-vec/dist/vec0.so" "$EXT_LIB/sqlitevec/vec0.so"
install -Dm644 "$WORK/sqlite-vec/sqlite-vec.h" "$EXT_INC/SqliteVec/sqlite-vec.h"

# ── 5. llama.cpp (every CPU variant + Vulkan; no CUDA) ───────────────────
echo ">> Building llama.cpp"
LLAMA_SRC="$WORK/llama.cpp"
LLAMA_BUILD="$WORK/llama-build"
cmake -S "$LLAMA_SRC" -B "$LLAMA_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DLLAMA_BUILD_COMMON=ON \
  -DLLAMA_BUILD_MTMD=ON \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_SERVER=OFF \
  -DLLAMA_CURL=OFF \
  -DGGML_BACKEND_DL=ON \
  -DGGML_NATIVE=OFF \
  -DGGML_CPU_ALL_VARIANTS=ON \
  -DGGML_VULKAN=ON \
  -DGGML_CUDA=OFF
cmake --build "$LLAMA_BUILD" -j"$JOBS" \
  --target llama --target mtmd --target llama-common --target ggml --target ggml-vulkan

LLAMA_LIB="$EXT_LIB/llamacpp"
mkdir -p "$LLAMA_LIB"
cp "$LLAMA_BUILD"/bin/lib*.so.*         "$LLAMA_LIB/"   # versioned core libs
cp "$LLAMA_BUILD"/bin/libggml-cpu-*.so  "$LLAMA_LIB/"   # every CPU variant
cp "$LLAMA_BUILD"/bin/libggml-vulkan.so "$LLAMA_LIB/"
find "$LLAMA_LIB" -maxdepth 1 -type l -delete

LLAMA_INC="$EXT_INC/llamacpp"
rm -f "$LLAMA_INC"/*.h
rm -rf "$LLAMA_INC/common"
mkdir -p "$LLAMA_INC/common/jinja"
cp "$LLAMA_SRC"/include/llama.h \
   "$LLAMA_SRC"/include/llama-cpp.h \
   "$LLAMA_SRC"/ggml/include/*.h \
   "$LLAMA_SRC"/tools/mtmd/mtmd.h \
   "$LLAMA_SRC"/tools/mtmd/mtmd-helper.h  "$LLAMA_INC/"
cp "$LLAMA_SRC"/common/{chat,common,sampling,peg-parser,json,json-schema}.h "$LLAMA_INC/common/"
cp "$LLAMA_SRC"/common/jinja/*.h "$LLAMA_INC/common/jinja/"

echo ""
echo ">> Dependencies staged in $EXT_LIB"
ls -la "$EXT_LIB" "$LLAMA_LIB"
