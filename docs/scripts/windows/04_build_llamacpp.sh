#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────
# Build llama.cpp as shared libraries (.dll) for Windows/MSYS2
#
# Builds 3 backend variants: CPU, CUDA, Vulkan.
# Each variant produces a set of .dll + .dll.a import libs.
#
# Output (per backend):
#   external/lib/windows/llamacpp/<backend>/
#     llama.dll, llama.dll.a
#     llama-common.dll, llama-common.dll.a
#     mtmd.dll, mtmd.dll.a
#     ggml.dll, ggml.dll.a
#     ggml-base.dll, ggml-base.dll.a
#     ggml-cpu.dll, ggml-cpu.dll.a
#     ggml-<backend>.dll  (only for cuda/vulkan)
#
# Usage:
#   ./04_build_llamacpp.sh [SOURCE_ROOT] [backends...]
#
#   backends: cpu | cuda | vulkan  (default: all three)
#   CPU is always built; cuda/vulkan are skipped if the backend flag is OFF.
#
# Run inside MSYS2 CLANG-UCRT64 shell.
# ─────────────────────────────────────────────────────────────────
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HODEIA="$(cd "$SCRIPT_DIR/../../../.." && pwd)"

# Source location (auto-detect, or override via $1)
SRC="${1:-/c/Dependencies}"
LLAMA_SRC="$SRC/llamacpp"

# Backend selection: pass "cpu cuda" or "cpu vulkan" etc. (default: all)
BACKENDS="${@:2:}"
if [ -z "$BACKENDS" ]; then
  BACKENDS="cpu cuda vulkan"
fi

EXT_LIB="$HODEIA/external/lib/windows"
EXT_INC="$HODEIA/external/include"

JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

# Shared DLLs to copy from each backend build
SHARED_FILES=(
  llama.dll llama.dll.a
  llama-common.dll llama-common.dll.a
  mtmd.dll mtmd.dll.a
  ggml.dll ggml.dll.a
  ggml-base.dll ggml-base.dll.a
  ggml-cpu.dll ggml-cpu.dll.a
)

# Backend-specific DLLs
declare -A BACKEND_FILES
BACKEND_FILES[cuda]="ggml-cuda.dll ggml-cuda.dll.a"
BACKEND_FILES[vulkan]="ggml-vulkan.dll ggml-vulkan.dll.a"

echo "=== Hodeia root : $HODEIA"
echo "=== llama.cpp src : $LLAMA_SRC"
echo "=== Backends      : $BACKENDS"
echo "=== Jobs          : $JOBS"
echo ""

# ─── Verify source ───────────────────────────────────────────
if [ ! -d "$LLAMA_SRC" ]; then
  echo "ERROR: llama.cpp source not found at $LLAMA_SRC"
  echo "  Hint: pass the source root as \$1"
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

  # Start with common flags
  local cmake_cmd=(cmake .. "${COMMON_CMAKE_FLAGS[@]}")

  # Add backend-specific flags
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

  # Run CMake
  "${cmake_cmd[@]}"

  cmake --build . -- -j"$JOBS"

  echo "  ✓ Build done."

  # ─── Copy DLLs ───────────────────────────────────────────
  mkdir -p "$install_dir"

  # Find the build output directory (llama.cpp puts DLLs in the build root)
  local out_dir="$build_dir/bin"
  if [ ! -d "$out_dir" ]; then
    # Fallback: DLLs may be at build root
    out_dir="$build_dir"
  fi

  # Copy shared (common) DLLs
  local copied=0
  for dll in "${SHARED_FILES[@]}"; do
    if [ -f "$out_dir/$dll" ]; then
      cp "$out_dir/$dll" "$install_dir/$dll"
      ((copied++))
    fi
  done

  # Copy backend-specific DLLs
  if [ -n "${BACKEND_FILES[$backend]:-}" ]; then
    for dll in ${BACKEND_FILES[$backend]}; do
      if [ -f "$out_dir/$dll" ]; then
        cp "$out_dir/$dll" "$install_dir/$dll"
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
echo ""
echo "━━━ Summary ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
for backend in $BACKENDS; do
  dir="$EXT_LIB/llamacpp/${backend}"
  if [ -d "$dir" ]; then
    count=$(ls -1 "$dir"/*.dll 2>/dev/null | wc -l)
    echo "  ${backend}: $count DLLs in $dir"
  fi
done
echo ""
echo "Done."
