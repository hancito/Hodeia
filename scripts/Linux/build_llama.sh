#!/bin/bash
# build_llama.sh
# Builds llama.cpp ONCE (CPU + CUDA + Vulkan combined) with GGML_BACKEND_DL +
# GGML_CPU_ALL_VARIANTS, then stages every resulting .so into a SINGLE flat
# directory: external/lib/linux/llamacpp/.
#
# This mirrors scripts/build-llama.ps1 (the Windows build) so the Linux app has
# the same properties that fixed the Windows inference bugs:
#   * One combined configure => every ggml backend links ONE shared ggml-base
#     ABI.  (Three isolated builds would produce three divergent ggml-base
#     .so with the same SONAME.)
#   * GGML_BACKEND_DL=ON  => each backend is a runtime-loadable MODULE that
#     ggml_backend_load_all() finds by scanning the exe dir for libggml-*.so
#     (it picks the best CPU variant by ggml_backend_score, and auto-detects
#     CUDA / Vulkan the same way as on Windows).
#   * GGML_CPU_ALL_VARIANTS=ON => every ggml-cpu-<microarch>.so is compiled and
#     loaded at runtime, so the host CPU (e.g. AVX-512 on a Zen 5) is matched
#     instead of building for the build host only.
#
# CUDA / Vulkan are enabled by default but degrade gracefully: if a toolkit /
# SDK / loader is missing, CMake configures that backend OFF (with a warning)
# and the build still succeeds.  The app detects each backend at runtime by the
# presence of its .so next to the binary.
#
# Usage:
#   ./build_llama.sh
#   ./build_llama.sh -s /path/to/llama.cpp -o /path/to/Hodeia
#   ./build_llama.sh -b cpu,vulkan          # skip CUDA
#   ./build_llama.sh -b cpu                 # CPU only

set -euo pipefail

# ── Defaults (override with flags) ─────────────────────────────────────────
LLAMA_DIR="/run/media/hancito/VAULT/Developpement/Dependencies/llama.cpp"
HODEIA="/home/hancito/LLM_Stuff/Hodeia"
BACKENDS=("cpu" "cuda" "vulkan")
CUDA_TOOLKIT="${CUDA_PATH:-}"

usage() {
    grep '^#' "$0" | sed 's/^# \{0,1\}//'
    exit 0
}

while getopts "s:o:b:h" opt; do
    case $opt in
        s) LLAMA_DIR="$OPTARG" ;;
        o) HODEIA="$OPTARG" ;;
        b) BACKENDS=(${OPTARG//,/ }) ;;
        h) usage ;;
        *) usage ;;
    esac
done

BUILD_ROOT="${HODEIA}/build_llama"
EXT_LIB="$HODEIA/external/lib/linux/llamacpp"
EXT_INC="$HODEIA/external/include/llamacpp"

[ -d "$LLAMA_DIR" ] || { echo "❌ llama.cpp source not found: $LLAMA_DIR"; exit 1; }

# ── Locate CUDA toolkit (if requested) ─────────────────────────────────────
CUDAFLAGS=""
if printf '%s\n' "${BACKENDS[@]}" | grep -q 'cuda'; then
    if [ -z "$CUDA_TOOLKIT" ]; then
        for d in /opt/cuda /usr/local/cuda; do
            [ -x "$d/bin/nvcc" ] && { CUDA_TOOLKIT="$d"; break; }
        done
    fi
    if [ -n "$CUDA_TOOLKIT" ] && [ -x "$CUDA_TOOLKIT/bin/nvcc" ]; then
        CUDAFLAGS="-DGGML_CUDA=ON"
        echo ">> CUDA enabled: $CUDA_TOOLKIT"
    else
        echo ">> WARNING: CUDA requested but no toolkit/nvcc found; configuring with GGML_CUDA=OFF"
        CUDAFLAGS="-DGGML_CUDA=OFF"
    fi
fi

# ── Locate Vulkan loader (if requested) ─────────────────────────────────────
# ggml's Vulkan backend can build against just the ICD (no full SDK) on Linux;
# if nothing is present CMake warns and configures it OFF.  We force ON and let
# CMake decide, mirroring the Windows behaviour.
VULKANFLAG="-DGGML_VULKAN=OFF"
if printf '%s\n' "${BACKENDS[@]}" | grep -q 'vulkan'; then
    VULKANFLAG="-DGGML_VULKAN=ON"
    echo ">> Vulkan requested (GGML_VULKAN=ON)"
fi

echo "══════════════════════════════════════════════════════════════"
echo "  llama.cpp combined build: ${BACKENDS[*]}"
echo "  source : $LLAMA_DIR"
echo "  staging: $EXT_LIB"
echo "══════════════════════════════════════════════════════════════"

# ── Fresh single build tree ────────────────────────────────────────────────
rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT"

# sccache (compiler cache).  The build tree is wiped every run, so without a
# cache everything is recompiled from scratch.  Enabling it makes repeat builds
# near-instant for unchanged sources.  No-op if sccache is not on PATH.
#
# We set ONLY the C and C++ compiler launchers (never a CUDA launcher).  This
# also keeps llama.cpp's own ccache auto-detection inert: ggml/src/CMakeLists.txt
# only sets a GLOBAL RULE_LAUNCH_COMPILE (the thing that would wrap nvcc) when
# GGML_CCACHE is ON AND no C/C++ launcher is set.  Because we always set the
# C/C++ launchers, that global rule never fires and sccache never touches nvcc.
# So no GGML_CCACHE=OFF is needed here — sccache handles nvcc fine.
# Plain string (not an array) so the script stays POSIX-sh compatible (the
# Zed agent terminal runs files with `sh`, which chokes on `arr=()`).  Unquoted
# word-splitting below yields the two -D...=sccache flags.
SCCACHE_FLAGS=""
if command -v sccache >/dev/null 2>&1; then
    SCCACHE_FLAGS="-DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache"
    echo ">> sccache detected - enabling compiler cache"
fi

# GGML_NATIVE=OFF: for CUDA this selects llama.cpp's full curated arch list
# (50..121, Maxwell→Blackwell) instead of only the build host's GPU.  For CPU
# it is a no-op (GGML_CPU_ALL_VARIANTS handles microarch selection at runtime).
cmake -S "$LLAMA_DIR" -B "$BUILD_ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=ON \
    -DLLAMA_BUILD_COMMON=ON \
    -DLLAMA_BUILD_MTMD=ON \
    -DGGML_BACKEND_DL=ON \
    -DGGML_CPU_ALL_VARIANTS=ON \
    -DGGML_NATIVE=OFF \
    $CUDAFLAGS \
    $VULKANFLAG \
    $SCCACHE_FLAGS

# Build the core libs.  Building "ggml" pulls in every ggml-cpu-* variant (the
# build system sets add_dependencies(ggml, <variant>)); we add the GPU backend
# targets explicitly.
TARGETS=(--target llama --target mtmd --target llama-common --target ggml)
printf '%s\n' "${BACKENDS[@]}" | grep -q 'cuda'   && TARGETS+=(--target ggml-cuda)
printf '%s\n' "${BACKENDS[@]}" | grep -q 'vulkan' && TARGETS+=(--target ggml-vulkan)

cmake --build "$BUILD_ROOT" "${TARGETS[@]}" --config Release -j"$(nproc)"

# ── Stage all backend .so into the single flat dir ─────────────────────────
# All .so land in $BUILD_ROOT/bin (CMAKE_RUNTIME_OUTPUT_DIRECTORY).  CMake
# writes them in two forms:
#   * core libs (llama / llama-common / mtmd / ggml / ggml-base) — versioned
#     `libX.so.0` + `libX.so.0.N.M`,
#   * runtime-loadable MODULE backends (every ggml-cpu-<microarch> variant and
#     ggml-cuda / ggml-vulkan) — UNVERSIONED `libX.so` (a MODULE can't carry a
#     versioned SONAME).
# We need BOTH: the app links the versioned core libs + ggml-cpu-x64, and ggml
# dlopens the unversioned variants + GPU modules at runtime.  Copy them all,
# then drop only the dev symlinks CMake emits (the app recreates libX.so ->
# libX.so.0 per deployed lib at build time).
rm -rf "$EXT_LIB"
mkdir -p "$EXT_LIB"
cp "$BUILD_ROOT"/bin/lib*.so.*   "$EXT_LIB/"   # versioned core libs
cp "$BUILD_ROOT"/bin/libggml-cpu-*.so   "$EXT_LIB/" 2>/dev/null  # CPU variants (unversioned)
cp "$BUILD_ROOT"/bin/libggml-cuda.so    "$EXT_LIB/" 2>/dev/null  # CUDA backend
cp "$BUILD_ROOT"/bin/libggml-vulkan.so  "$EXT_LIB/" 2>/dev/null  # Vulkan backend
# Remove stray dev symlinks CMake may have emitted (the app recreates them).
find "$EXT_LIB" -maxdepth 1 -type l -delete

# ── Copy the headers Hodeia actually needs (independent of backend) ─────────
rm -f "$EXT_INC"/*.h
rm -rf "$EXT_INC/common"
mkdir -p "$EXT_INC/common/jinja"

# llama.cpp core (llama-cpp.h is pulled in by common.h)
cp "$LLAMA_DIR"/include/llama.h        "$EXT_INC/"
cp "$LLAMA_DIR"/include/llama-cpp.h    "$EXT_INC/"

# ggml — all headers (llama.h includes ggml.h/ggml-cpu.h etc. via quoted
# includes; the in-tree copies must shadow the distro's /usr/include/ggml*)
cp "$LLAMA_DIR"/ggml/include/*.h           "$EXT_INC/"

# common/ — direct includes + transitive deps
cp "$LLAMA_DIR"/common/chat.h                "$EXT_INC/common/"
cp "$LLAMA_DIR"/common/common.h              "$EXT_INC/common/"
cp "$LLAMA_DIR"/common/sampling.h            "$EXT_INC/common/"
cp "$LLAMA_DIR"/common/peg-parser.h          "$EXT_INC/common/"
cp "$LLAMA_DIR"/common/json.h                "$EXT_INC/common/"
cp "$LLAMA_DIR"/common/json-schema.h         "$EXT_INC/common/"

# jinja/ — all .h files (chat.h pulls in the whole chain)
cp "$LLAMA_DIR"/common/jinja/*.h            "$EXT_INC/common/jinja/"

# mtmd
cp "$LLAMA_DIR"/tools/mtmd/mtmd.h           "$EXT_INC/"
cp "$LLAMA_DIR"/tools/mtmd/mtmd-helper.h    "$EXT_INC/"

echo ""
echo "══════════════════════════════════════════════"
echo "  Combined build complete."
echo "  Libs : $EXT_LIB/ ($(ls "$EXT_LIB" | wc -l) .so files)"
echo "  Inc  : $EXT_INC/"
echo "══════════════════════════════════════════════"
