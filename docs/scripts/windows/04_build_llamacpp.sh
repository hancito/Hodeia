#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# Build llama.cpp as shared libraries (.dll) for Windows/MSYS2
#
# Builds one or more backend variants (CPU, CUDA, Vulkan).
# Each variant produces .dll + .dll.a import libs.
#
# Output (per backend):
#   external/lib/windows/llamacpp/<backend>/
#     llama.dll, llama.dll.a
#     llama-common.dll, llama-common.dll.a
#     mtmd.dll, mtmd.dll.a
#     ggml.dll, ggml.dll.a
#     ggml-base.dll, ggml-base.dll.a
#     ggml-cpu.dll, ggml-cpu.dll.a
#     ggml-cuda.dll (+ .dll.a)    ← only for cuda
#     ggml-vulkan.dll (+ .dll.a)  ← only for vulkan
#
# Usage:
#   ./04_build_llamacpp.sh [SOURCE_ROOT] [backends...]
#
#   SOURCE_ROOT: directory containing the llama.cpp source
#   backends:    cpu | cuda | vulkan  (default: cpu)
#
# Run inside MSYS2 UCRT64 shell.
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HODEIA="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

SRC="${1:-/c/Dependencies}"
LLAMA_SRC="$SRC/llama.cpp"
if [ ! -d "$LLAMA_SRC" ]; then
  # Try the older directory name
  LLAMA_SRC="$SRC/llamacpp"
fi

BACKENDS="${*:2}"
if [ -z "$BACKENDS" ]; then
  BACKENDS="cpu"
fi

EXT_LIB="$HODEIA/external/lib/windows"
EXT_INC="$HODEIA/external/include"

JOBS="$(nproc 2>/dev/null || echo 4)"

# Shared DLLs + import libs to copy from each backend build
SHARED_FILES=(
  llama.dll llama.dll.a
  llama-common.dll llama-common.dll.a
  mtmd.dll mtmd.dll.a
  ggml.dll ggml.dll.a
  ggml-base.dll ggml-base.dll.a
  ggml-cpu.dll ggml-cpu.dll.a
)

declare -A BACKEND_FILES
BACKEND_FILES[cuda]="ggml-cuda.dll ggml-cuda.dll.a"
BACKEND_FILES[vulkan]="ggml-vulkan.dll ggml-vulkan.dll.a"

echo "══════════════════════════════════════════════════════════════"
echo "  llama.cpp — MSYS2 UCRT64"
echo "══════════════════════════════════════════════════════════════"
echo "  Source   : $LLAMA_SRC"
echo "  Backends : $BACKENDS"
echo "  Jobs     : $JOBS"
echo "  Output   : $EXT_LIB/llamacpp/"
echo ""

# ─── Verify source ───────────────────────────────────────────
if [ ! -d "$LLAMA_SRC" ]; then
  echo "ERROR: llama.cpp source not found at $LLAMA_SRC"
  echo "  Hint: pass the source root as \$1, e.g.:"
  echo "    bash 04_build_llamacpp.sh /c/Dependencies cpu"
  exit 1
fi

# ─── Common CMake flags ──────────────────────────────────────
COMMON_CMAKE_FLAGS=(
  -G Ninja
  -DCMAKE_C_COMPILER=clang
  -DCMAKE_CXX_COMPILER=clang++
  -DCMAKE_BUILD_TYPE=Release
  -DBUILD_SHARED_LIBS=ON
  -DLLAMA_OPENMP=ON
  -DLLAMA_BUILD_COMMON=ON
  -DLLAMA_BUILD_MTMD=ON
  -DLLAMA_BUILD_TESTS=OFF
  -DLLAMA_BUILD_EXAMPLES=OFF
  -DLLAMA_BUILD_SERVER=OFF
  -DLLAMA_BUILD_TOOLS=OFF
  -DLLAMA_BLAS=OFF
  -DLLAMA_LASX=OFF
  -DLLAMA_LASX_CMOFMA=OFF
  -DLLAMA_RPC=OFF
)

# ─── Build helper ────────────────────────────────────────────
build_backend() {
  local backend="$1"
  local build_dir="$LLAMA_SRC/build-${backend}"
  local install_dir="$EXT_LIB/llamacpp/${backend}"

  echo ""
  echo "━━━ Building llama.cpp [${backend}] ━━━━━━━━━━━━━━━━━━━━━━━━━"

  mkdir -p "$build_dir"
  cd "$build_dir"
  rm -f CMakeCache.txt

  local cmake_cmd=(cmake .. "${COMMON_CMAKE_FLAGS[@]}")

  case "$backend" in
    cpu)
      cmake_cmd+=(-DLLAMA_CUDA=OFF -DLLAMA_VULKAN=OFF)
      ;;
    cuda)
      cmake_cmd+=(-DLLAMA_CUDA=ON -DLLAMA_VULKAN=OFF)
      ;;
    vulkan)
      cmake_cmd+=(-DLLAMA_CUDA=OFF -DLLAMA_VULKAN=ON)
      ;;
  esac

  "${cmake_cmd[@]}"
  cmake --build . -- -j"$JOBS"

  echo "  ✓ Build done."

  # ─── Copy DLLs + import libs ─────────────────────────────
  mkdir -p "$install_dir"

  # llama.cpp puts DLLs in the build root or bin/
  local out_dir="$build_dir/bin"
  [ -d "$out_dir" ] || out_dir="$build_dir"

  local copied=0
  for f in "${SHARED_FILES[@]}"; do
    if [ -f "$out_dir/$f" ]; then
      cp "$out_dir/$f" "$install_dir/$f"
      ((copied++))
    fi
  done

  if [ -n "${BACKEND_FILES[$backend]:-}" ]; then
    for f in ${BACKEND_FILES[$backend]}; do
      if [ -f "$out_dir/$f" ]; then
        cp "$out_dir/$f" "$install_dir/$f"
        ((copied++))
      fi
    done
  fi

  echo "  ✓ Copied $copied files → $install_dir/"
  ls -lh "$install_dir/"
  echo ""
}

# ─── Copy headers once ───────────────────────────────────────
echo "━━━ Installing llama.cpp headers ━━━━━━━━━━━━━━━━━━━━━━━━━━━"

mkdir -p "$EXT_INC/llamacpp"
# llama.cpp headers
if [ -d "$LLAMA_SRC/include" ]; then
  cp -r "$LLAMA_SRC/include/"* "$EXT_INC/llamacpp/"
fi
# common headers (used by Hodeia)
if [ -d "$LLAMA_SRC/common/include" ]; then
  mkdir -p "$EXT_INC/llamacpp/common"
  cp -r "$LLAMA_SRC/common/include/"* "$EXT_INC/llamacpp/common/"
fi
echo "  ✓ Headers → $EXT_INC/llamacpp/"

# ─── Build each backend ─────────────────────────────────────
for backend in $BACKENDS; do
  case "$backend" in
    cpu|cuda|vulkan)
      build_backend "$backend"
      ;;
    *)
      echo "ERROR: Unknown backend '$backend' (expected cpu, cuda, or vulkan)"
      exit 1
      ;;
  esac
done

# ─── Summary ─────────────────────────────────────────────────
echo "══════════════════════════════════════════════════════════════"
echo "  Summary"
echo "══════════════════════════════════════════════════════════════"
for backend in $BACKENDS; do
  dir="$EXT_LIB/llamacpp/${backend}"
  if [ -d "$dir" ]; then
    dll_count=$(ls "$dir"/*.dll 2>/dev/null | wc -l)
    lib_count=$(ls "$dir"/*.dll.a 2>/dev/null | wc -l)
    echo "  ${backend}: ${dll_count} DLLs, ${lib_count} import libs"
  fi
done
echo ""
echo "Done. Next: bash 05_build_sqlitevec.sh"
