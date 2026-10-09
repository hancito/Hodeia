# ---------------------------------------------------------------------------
# PlatformMac.cmake
# macOS (Apple Silicon) build rules.  Mirrors the Linux schema
# (cmake/PlatformLinux.cmake) so every platform stages llama.cpp the same way:
#
#   * llama.cpp is built ONCE (CPU + Metal combined) with GGML_BACKEND_DL +
#     GGML_CPU_ALL_VARIANTS and staged into a SINGLE flat directory:
#       external/lib/mac/llamacpp/
#     Every .dylib — core, every ggml-cpu-<microarch> variant, and the Metal
#     GPU backend — shares one ggml-base ABI and lives in the same folder.
#   * The app links the CORE libs only (llama, llama-common, mtmd, ggml,
#     ggml-base) + the Metal backend if present.  Every CPU microarch variant
#     is a runtime-loadable MODULE: ggml_backend_load_all() dlopens the best
#     ggml-cpu-<microarch> for the host CPU and auto-detects Metal.
#   * GPU backend on macOS is Metal only (no CUDA / Vulkan), so there is no
#     LLAMA_FORCE_BACKEND option (Linux has one for CPU/CUDA/Vulkan).
#   * Deployment: copy dylibs next to the binary, set @executable_path install
#     names (install_name_tool), ship vec0.dylib + libomp.dylib, run
#     macdeployqt.  (No patchelf — that is Linux-only.)
#
# NOTE: macOS support is EXPERIMENTAL and the app is NOT yet available for
# macOS.  This file follows the Linux schema but has not been validated on a
# real Apple-Silicon build host — treat the build as untested.
#
# Included by the top-level CMakeLists.txt AFTER HODEIA_PLATFORM_MAC=ON.
# ---------------------------------------------------------------------------

set(HODEIA_PLATFORM_MAC ON)

message(WARNING
  "macOS support is EXPERIMENTAL — the app is not yet available for macOS. "
  "This build path mirrors the (tested) Linux schema but is UNTESTED.")

# ===========================================================================
# A. Shared-library name mapping + library paths
# ===========================================================================
set(_PLAT              "mac")
set(_SHARED_LLAMA      "libllama.dylib")
set(_SHARED_LLAMA_COMM "libllama-common.dylib")
set(_SHARED_MTMD       "libmtmd.dylib")
set(_SHARED_GGML       "libggml.dylib")
set(_SHARED_GGML_BASE  "libggml-base.dylib")
set(_SHARED_GGML_METAL "libggml-metal.dylib")
set(_VECTOREC_EXT      "vec0.dylib")
set(_STATIC_EXT        ".a")

# In-tree static third-party libs (MuPDF / Tesseract / Leptonica).
set(_MUPDF_LIB         "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf${_STATIC_EXT}")
set(_MUPDF_THIRD_LIB   "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf-third${_STATIC_EXT}")
set(_MUPDF_THREADS_LIB "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf-threads${_STATIC_EXT}")
set(_TESS_LIB          "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libtesseract${_STATIC_EXT}")
set(_LEPTO_LIB         "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libleptonica${_STATIC_EXT}")
# llama.cpp — single flat dir (all backends), mirroring the Linux build.
set(_LLAMA_BASE        "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/llamacpp")
set(_SQLITEVEC_LIB     "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/sqlitevec/${_VECTOREC_EXT}")

# ===========================================================================
# B. Backend — Metal only (the only GPU backend on macOS)
# ===========================================================================
# There is no CUDA / Vulkan on macOS, so unlike Linux there is no
# LLAMA_FORCE_BACKEND option — the backend set is always "cpu (all variants) +
# metal", and the actual CPU microarch + Metal selection happens at RUNTIME via
# ggml_backend_load_all() (see localbackend.cpp).  The HODEIA_BACKEND_* compile
# definition mirrors Linux and is informational for the app.
set(LLAMA_BACKEND "metal")
message(STATUS "Backend (macOS): metal (combined CPU + Metal build, runtime selection)")

find_library(METAL_FRAMEWORK Metal)
if(METAL_FRAMEWORK)
  message(STATUS "Metal framework found")
else()
  message(WARNING "Metal framework not found — the Metal backend will be unavailable at runtime")
endif()

string(TOUPPER "${LLAMA_BACKEND}" _upper)
add_compile_definitions("HODEIA_BACKEND_${_upper}")

set(_LLAMA_LINK_DIR "${_LLAMA_BASE}")

# ===========================================================================
# B2. llama.cpp backend-specific shared lib (linked only if present)
# ===========================================================================
# The Metal backend is a runtime-loadable MODULE; link it if the dylib was
# staged, otherwise the app simply runs CPU-only (ggml-metal is not dlopened).
set(_BACKEND_SHARED_LIBS "")
if(EXISTS "${_LLAMA_LINK_DIR}/${_SHARED_GGML_METAL}")
  list(APPEND _BACKEND_SHARED_LIBS "${_LLAMA_LINK_DIR}/${_SHARED_GGML_METAL}")
endif()

# --- Link variables consumed by src/CMakeLists.txt ---
# The app links the CORE libs only.  The CPU backend is a runtime-loadable
# MODULE: ggml-base does NOT link any ggml-cpu-* dylib (no such NEEDED entry) —
# instead ggml_backend_load_all() dlopens the best ggml-cpu-<microarch> variant
# at runtime via ggml_backend_score.  This matches the Linux and Windows builds,
# which also link no ggml-cpu-* lib.  (Linking ggml-cpu-x64 here would collide
# with the unversioned module dylib of the same name at deploy.)
set(LLAMA_LINK_LIBS
    ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA_COMM}
    ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA}
    ${_LLAMA_LINK_DIR}/${_SHARED_MTMD}
    ${_LLAMA_LINK_DIR}/${_SHARED_GGML}
    ${_LLAMA_LINK_DIR}/${_SHARED_GGML_BASE})

if(_BACKEND_SHARED_LIBS)
  list(APPEND LLAMA_LINK_LIBS ${_BACKEND_SHARED_LIBS})
endif()

# ===========================================================================
# B3. In-tree static third-party libs (MuPDF / Tesseract / Leptonica)
#     Linked into HodeiaEngine in src/CMakeLists.txt.  MuPDF is built with
#     MUPDF_BUILD_THIRD_PARTY=ON so it embeds all image-codec deps; the OCR
#     stack (tesseract -> leptonica) is linked directly.
# ===========================================================================
set(MUPDF_LIB_TARGETS
    ${_MUPDF_LIB}
    ${_MUPDF_THIRD_LIB}
    ${_MUPDF_THREADS_LIB})

set(TESS_LEPTO_TARGETS
    ${_TESS_LIB}
    ${_LEPTO_LIB})

# ===========================================================================
# C. Platform-specific system libraries
# ===========================================================================
set(HODEIA_SYSTEM_LIBS ${METAL_FRAMEWORK} m)

# OpenMP (ggml-cpu) — the shared runtime must be shipped alongside the app.
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
  message(STATUS "OpenMP found")
  get_target_property(_OMP_LIB OpenMP::OpenMP_C LOCATION)
  get_filename_component(_OMP_DIR "${_OMP_LIB}" DIRECTORY)
  # libomp.dylib sits beside libomp.a inside the LLVM prefix/lib/
  set(_OMP_DYLIB "${_OMP_DIR}/libomp.dylib")
else()
  message(STATUS "OpenMP not found — ggml-cpu will run single-threaded")
endif()

# ===========================================================================
# D. POST_BUILD — deployment (requires the Hodeia target to exist)
#    Called from src/CMakeLists.txt after the target is created.
#    Mirrors hodeia_linux_deploy: copy the whole llama.cpp .dylib set (core +
#    every CPU variant + Metal) next to the binary, fix @executable_path
#    install names, ship vec0.dylib + libomp.dylib, then macdeployqt.
# ===========================================================================
function(hodeia_mac_deploy TARGET)
  # The executable lives in <app>.app/Contents/MacOS/; assets go next to it.

  # --- Translations (.qm files) ---
  foreach(_qm ${HODEIA_QM_FILES})
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${TARGET}>/i18n
        COMMAND ${CMAKE_COMMAND} -E copy
            ${_qm} $<TARGET_FILE_DIR:${TARGET}>/i18n/
        COMMENT "Copying translations")
  endforeach()

  # --- Themes & fonts ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/ui/themes $<TARGET_FILE_DIR:${TARGET}>/ui/themes
      COMMENT "Copying themes")
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Fonts $<TARGET_FILE_DIR:${TARGET}>/Fonts
      COMMENT "Copying fonts")

  # --- Tesseract tessdata ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Resources/tessdata $<TARGET_FILE_DIR:${TARGET}>/tessdata
      COMMENT "Copying tessdata")

  # --- Default embedding model (seeded into the data dir on first run) ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Resources/Embedders
          $<TARGET_FILE_DIR:${TARGET}>/Resources/Embedders
      COMMENT "Copying default embedding model")

  # --- llama.cpp .dylib (single flat dir, all backends) ---
  # Every backend .dylib — core (libX.dylib) AND the unversioned MODULE libs
  # (ggml-cpu-<microarch> variants, ggml-metal) — must sit next to the binary
  # so ggml can dlopen the best CPU variant and auto-detect Metal at runtime.
  # They all live in the single flat ${_LLAMA_LINK_DIR} dir, so copy the whole
  # set.  (A glob of just the core names would drop the CPU-variant modules.)
  file(GLOB _LLAMA_DEPLOY_DYLIB "${_LLAMA_LINK_DIR}/*.dylib")
  list(REMOVE_DUPLICATES _LLAMA_DEPLOY_DYLIB)
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_LLAMA_DEPLOY_DYLIB}
          $<TARGET_FILE_DIR:${TARGET}>
      COMMENT "Copying llama.cpp .dylib (CPU variants + Metal + core)")

  # --- Fix @rpath → @executable_path on every deployed .dylib ---
  # macOS dylibs carry an install name; point each at @executable_path so the
  # app finds its siblings next to the binary without an @rpath entry.  (This
  # is the macOS equivalent of the Linux patchelf --set-rpath '$$ORIGIN'.)
  set(_MAC_INSTALL_NAME_CMDS "")
  foreach(_dy IN LISTS _LLAMA_DEPLOY_DYLIB)
    get_filename_component(_dy_NAME "${_dy}" NAME)
    list(APPEND _MAC_INSTALL_NAME_CMDS
      COMMAND install_name_tool -id @executable_path/${_dy_NAME} $<TARGET_FILE_DIR:${TARGET}>/${_dy_NAME})
  endforeach()
  add_custom_command(TARGET ${TARGET} POST_BUILD
      ${_MAC_INSTALL_NAME_CMDS}
      COMMENT "Setting @executable_path install names on llama .dylib")

  # --- sqlite-vec extension ---
  if(EXISTS "${_SQLITEVEC_LIB}")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_SQLITEVEC_LIB}"
            $<TARGET_FILE_DIR:${TARGET}>/vec0.dylib
        COMMENT "Copying sqlite-vec extension")
  else()
    message(WARNING "sqlite-vec ${_VECTOREC_EXT} not found at ${_SQLITEVEC_LIB} — RAG vector search will be unavailable.")
  endif()

  # --- OpenMP runtime ---
  if(OpenMP_CXX_FOUND AND EXISTS "${_OMP_DYLIB}")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_OMP_DYLIB}" $<TARGET_FILE_DIR:${TARGET}>/libomp.dylib
        COMMAND install_name_tool -id @executable_path/libomp.dylib
            $<TARGET_FILE_DIR:${TARGET}>/libomp.dylib
        COMMENT "Copying and fixing libomp.dylib")
  endif()

  # =========================================================================
  # macdeployqt — ships all Qt frameworks, plugins, and resources into the
  # .app bundle so the user gets a standalone, drag-and-drop application.
  # =========================================================================
  find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
      HINTS "${CMAKE_PREFIX_PATH}" "${Qt6_DIR}/../../../bin"
      PATH_SUFFIXES "bin")

  if(MACDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${MACDEPLOYQT_EXECUTABLE}
            $<TARGET_BUNDLE_DIR:${TARGET}>
            -always-overwrite
        COMMENT "Running macdeployqt (shipping Qt frameworks into .app)")
  else()
    message(WARNING "macdeployqt not found — Qt frameworks will NOT be bundled.\n"
      "Set CMAKE_PREFIX_PATH to your Qt installation prefix,\n"
      "or manually run macdeployqt on the built .app.")
  endif()
endfunction()
