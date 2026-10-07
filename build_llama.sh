#!/bin/bash
cd /run/media/hancito/VAULT/Developpement/Dependencies
# Configuration
LLAMA_DIR="./llama.cpp"
BUILD_ROOT="./build_llama"
HODEIA="/home/hancito/LLM_Stuff/Hodeia"
EXT_LIB="$HODEIA/external/lib/linux/llamacpp"
EXT_INC="$HODEIA/external/include/llamacpp"

# Backends to compile
# Format: "Name:CMake_Flags"
BACKENDS=(
    "cpu:-DGGML_CUDA=OFF -DGGML_VULKAN=OFF "
    "cuda:-DGGML_CUDA=ON"
    "vulkan:-DGGML_VULKAN=ON"
)

rm -rf "$BUILD_ROOT"
mkdir -p "$BUILD_ROOT"

for item in "${BACKENDS[@]}"; do
    IFS=":" read -r name flags <<< "$item"
    echo "--- Building Backend: $name ---"

    build_dir="$BUILD_ROOT/$name"
    mkdir -p "$build_dir"

    # Configure
    # Note: -DBUILD_SHARED_LIBS=ON produces libllama.so/dll
    cmake -S "$LLAMA_DIR" -B "$build_dir" \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DGGML_MTMD=ON\
        -DLLAMA_BUILD_COMMON=ON\
        $flags

    # Build only the library target to save time
    # Change this line:
# cmake --build "$build_dir" --target llama --config Release -j$(nproc)

# To this:
cmake --build "$build_dir" --target llama --target mtmd --target llama-common --config Release -j$(nproc)

    echo "✅ Finished $name. Libraries located in: $build_dir"

    # ── Clean & copy versioned .so files to Hodeia/external ──
    dest_lib="$EXT_LIB/$name"
    mkdir -p "$dest_lib"
    rm -f "$dest_lib"/lib*.so.*
    cp "$build_dir"/bin/lib*.so.* "$dest_lib/"
    echo "📦 Copied $name libs → $dest_lib/"

    # Clean & copy only the headers Hodeia actually needs
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

    echo "📦 Copied headers → $EXT_INC/"
done

echo ""
echo "══════════════════════════════════════════════"
echo "  All backends built & installed."
echo "  Libs : $EXT_LIB/"
echo "  Inc  : $EXT_INC/"
echo "══════════════════════════════════════════════"
