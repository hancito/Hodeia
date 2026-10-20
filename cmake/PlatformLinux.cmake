# ---------------------------------------------------------------------------
# PlatformLinux.cmake
# Linux build rules: static third-party libraries (.a) + system dependencies,
# GPU backend discovery (CUDA > Vulkan > CPU), llama.cpp shared-lib mapping,
# and the POST_BUILD deployment (copy/symlink/patchelf/vec0).
#
# Included by the top-level CMakeLists.txt AFTER HODEIA_PLATFORM_LINUX=ON.
# ---------------------------------------------------------------------------

set(HODEIA_PLATFORM_LINUX ON)

# ===========================================================================
# A. Shared-library name mapping + static library paths
# ===========================================================================
set(_PLAT              "linux")
set(_SHARED_LLAMA      "libllama.so.0")
set(_SHARED_LLAMA_COMM "libllama-common.so.0")
set(_SHARED_MTMD       "libmtmd.so.0")
set(_SHARED_GGML       "libggml.so.0")
set(_SHARED_GGML_BASE  "libggml-base.so.0")
set(_SHARED_GGML_CUDA  "libggml-cuda.so.0")
set(_SHARED_GGML_VK    "libggml-vulkan.so.0")
set(_SHARED_GGML_METAL "")
set(_VECTOREC_EXT      "vec0.so")
set(_STATIC_EXT        ".a")

# Platform-specific static library paths (in-tree, pre-compiled)
set(_MUPDF_LIB         "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf${_STATIC_EXT}")
set(_MUPDF_THIRD_LIB   "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf-third${_STATIC_EXT}")
set(_MUPDF_THREADS_LIB "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf-threads${_STATIC_EXT}")
set(_TESS_LIB          "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libtesseract${_STATIC_EXT}")
set(_LEPTO_LIB         "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libleptonica${_STATIC_EXT}")
set(_LLAMA_BASE        "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/llamacpp")
set(_SQLITEVEC_LIB     "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/sqlitevec/${_VECTOREC_EXT}")

# ===========================================================================
# B. GPU backend discovery (CUDA > Vulkan > CPU)
# ===========================================================================
option(LLAMA_FORCE_BACKEND "" "Override backend: cpu | cuda | vulkan")

set(CUDA_FOUND OFF)
find_library(HOST_CUDA    cuda   PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/cuda/lib64 /opt/cuda/lib64 NO_DEFAULT_PATH)
find_library(HOST_CUDART  cudart PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/cuda/lib64 /opt/cuda/lib64 NO_DEFAULT_PATH)
find_library(HOST_CUBLAS  cublas PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/cuda/lib64 /opt/cuda/lib64 NO_DEFAULT_PATH)
find_library(HOST_NCCL    nccl   PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/cuda/lib64 /opt/cuda/lib64 NO_DEFAULT_PATH)
if(HOST_CUDA AND HOST_CUDART AND HOST_CUBLAS)
  set(CUDA_FOUND ON)
  message(STATUS "CUDA toolkit found: ${HOST_CUDART}")
endif()

set(VULKAN_FOUND OFF)
find_library(HOST_VULKAN vulkan PATHS /usr/lib /usr/lib/x86_64-linux-gnu NO_DEFAULT_PATH)
if(HOST_VULKAN)
  set(VULKAN_FOUND ON)
  message(STATUS "Vulkan found: ${HOST_VULKAN}")
endif()

set(METAL_FRAMEWORK "")

# --- Pick the backend -------------------------------------------------------
set(LLAMA_BACKEND "")
if(LLAMA_FORCE_BACKEND)
  string(TOLOWER "${LLAMA_FORCE_BACKEND}" LLAMA_BACKEND)
  if(NOT LLAMA_BACKEND STREQUAL "cpu"
     AND NOT LLAMA_BACKEND STREQUAL "cuda"
     AND NOT LLAMA_BACKEND STREQUAL "vulkan"
     AND NOT LLAMA_BACKEND STREQUAL "all")
    message(FATAL_ERROR "LLAMA_FORCE_BACKEND must be cpu, cuda, vulkan, or all")
  endif()
  message(STATUS "Backend (forced): ${LLAMA_BACKEND}")
else()
  if(CUDA_FOUND)
    set(LLAMA_BACKEND "cuda")
  elseif(VULKAN_FOUND)
    set(LLAMA_BACKEND "vulkan")
  else()
    set(LLAMA_BACKEND "cpu")
  endif()
  message(STATUS "Backend (auto): ${LLAMA_BACKEND}")
endif()

string(TOUPPER "${LLAMA_BACKEND}" _upper)
add_compile_definitions("HODEIA_BACKEND_${_upper}")

# llama.cpp is built ONCE (CPU + CUDA + Vulkan combined) with GGML_BACKEND_DL +
# GGML_CPU_ALL_VARIANTS and staged into a SINGLE flat directory (mirroring the
# Windows build).  Every .so — core, every ggml-cpu-<microarch> variant, and
# the CUDA / Vulkan GPU backends — shares one ggml-base ABI and lives in the
# same folder.  GPU backend selection is done at RUNTIME via
# ggml_backend_load_all() (see localbackend.cpp): we deploy whichever GPU
# backends exist and link the ones present in the staging dir.  The
# auto/forced backend chosen above (LLAMA_BACKEND) is kept only to set the
# HODEIA_BACKEND_* compile definition.
set(_LLAMA_LINK_DIR "${_LLAMA_BASE}")

# ===========================================================================
# B2. llama.cpp backend-specific shared libs (linked only if present)
# ===========================================================================
set(_BACKEND_SHARED_LIBS "")
if(_SHARED_GGML_CUDA AND EXISTS "${_LLAMA_LINK_DIR}/${_SHARED_GGML_CUDA}")
  list(APPEND _BACKEND_SHARED_LIBS "${_LLAMA_LINK_DIR}/${_SHARED_GGML_CUDA}")
endif()
if(_SHARED_GGML_VK AND EXISTS "${_LLAMA_LINK_DIR}/${_SHARED_GGML_VK}")
  list(APPEND _BACKEND_SHARED_LIBS "${_LLAMA_LINK_DIR}/${_SHARED_GGML_VK}")
endif()

# --- Link variables consumed by src/CMakeLists.txt ---
# The app links the core libs only.  The CPU backend is a runtime-loadable
# MODULE: ggml-base does NOT link any ggml-cpu-* lib (it has no such NEEDED
# entry) — instead ggml_backend_load_all() dlopens the best ggml-cpu-<microarch>
# variant at runtime via ggml_backend_score.  This matches the Windows build,
# which also links no ggml-cpu-* import lib.  (Linking ggml-cpu-x64 here would
# collide with the unversioned module libX.so of the same name at deploy.)
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
#     Linked into HodeiaEngine in src/CMakeLists.txt.  MuPDF + its transitive
#     static deps (third/threads) and the OCR stack (tesseract -> leptonica)
#     are linked directly; their image-codec/threads deps come from the
#     system libs (png/jpeg/tiff/z, pthread).
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
# On Linux the app links the in-tree static third-party libs and a set of
# system shared libs (image codecs, pthread, dl, z).  Set here; linked in
# src/CMakeLists.txt so the target definitions stay in one place.
set(HODEIA_SYSTEM_LIBS pthread dl m z png jpeg tiff)

# OpenMP (ggml-cpu)
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
  message(STATUS "OpenMP found")
else()
  message(STATUS "OpenMP not found — ggml-cpu will run single-threaded")
endif()

# ===========================================================================
# E. POST_BUILD — deployment (requires the Hodeia target to exist)
#    Called from src/CMakeLists.txt after the target is created.
# ===========================================================================
function(hodeia_linux_deploy TARGET)
  # --- Translations (.qm files) ---
  foreach(_qm ${HODEIA_QM_FILES})
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${TARGET}>/i18n
        COMMAND ${CMAKE_COMMAND} -E copy
            ${_qm}
            $<TARGET_FILE_DIR:${TARGET}>/i18n/
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

  # --- Bundled model tree (Embedders/ + Summarizers/) ---
  # seedBundledModels() copies this from <exe>/Resources/Models/ into the user's
  # data-dir Models/ on first run (copy-if-missing).  copy_directory keeps
  # rebuilds cheap; the .gguf payloads are large but unchanged.
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Resources/Models
          $<TARGET_FILE_DIR:${TARGET}>/Resources/Models
      COMMENT "Copying bundled model tree")

  # --- llama.cpp .so (single flat dir, all backends) ---
  # Every backend .so — versioned core libs (libX.so.0 / libX.so.0.N.M) AND the
  # unversioned MODULE libs (libggml-cpu-<microarch>.so, libggml-cuda.so,
  # libggml-vulkan.so) — must sit next to the binary so ggml can dlopen the best
  # CPU variant and auto-detect the GPU at runtime.  They all live in the single
  # flat ${_LLAMA_LINK_DIR} dir (which contains ONLY these libs), so copy the
  # whole set.  (lib*.so.* alone would drop the unversioned modules — *.so.*
  # requires a dot after "so".)
  file(GLOB _LLAMA_DEPLOY_SO "${_LLAMA_LINK_DIR}/lib*.so" "${_LLAMA_LINK_DIR}/lib*.so.*")
  list(REMOVE_DUPLICATES _LLAMA_DEPLOY_SO)
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_LLAMA_DEPLOY_SO}
          $<TARGET_FILE_DIR:${TARGET}>
      COMMENT "Copying llama.cpp .so (CPU variants + Vulkan/CUDA + core)")

  # --- Unversioned symlinks (libX.so -> libX.so.0) ---
  # The linker resolves the .so.0 by SONAME, but create the unversioned dev
  # symlinks for every deployed .so (cheap, and keeps the dir self-consistent).
  file(GLOB _LLAMA_SYMLINK_SRC "${_LLAMA_LINK_DIR}/lib*.so.0")
  set(_SYMLINK_CMDS "")
  foreach(_so ${_LLAMA_SYMLINK_SRC})
    get_filename_component(_base "${_so}" NAME)   # libX.so.0
    string(REGEX REPLACE "\\.so\\.0$" ".so" _ln "${_base}")
    list(APPEND _SYMLINK_CMDS COMMAND ${CMAKE_COMMAND} -E create_symlink ${_base} $<TARGET_FILE_DIR:${TARGET}>/${_ln})
  endforeach()
  add_custom_command(TARGET ${TARGET} POST_BUILD
      ${_SYMLINK_CMDS}
      COMMENT "llama.cpp dev symlinks")

  # --- $ORIGIN rpath (patchelf) ---
  # Locate patchelf so CMake emits it as an absolute path (a bare "patchelf"
  # may be dropped by the generator); the .so already carry an empty RUNPATH
  # that patchelf --set-rpath replaces in place.  Every deployed .so needs
  # $ORIGIN so it can find its siblings next to the binary.
  find_program(PATCHELF_EXECUTABLE patchelf REQUIRED)
  # NOTE: the rpath value is "$$ORIGIN" — the doubled dollar is required so the
  # build system's shell does not expand $ORIGIN to the (unset) env var and
  # patch an empty rpath.  The .so already carry an empty RUNPATH that
  # --set-rpath replaces in place.
  set(_PATCH_RPATH "")
  foreach(_so ${_LLAMA_DEPLOY_SO})
    get_filename_component(_base "${_so}" NAME)
    list(APPEND _PATCH_RPATH COMMAND ${PATCHELF_EXECUTABLE} --set-rpath '$$ORIGIN' $<TARGET_FILE_DIR:${TARGET}>/${_base})
  endforeach()
  add_custom_command(TARGET ${TARGET} POST_BUILD
      ${_PATCH_RPATH}
      COMMENT "Fixing \$ORIGIN rpath on llama .so")

  # --- sqlite-vec ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_SQLITEVEC_LIB}
          $<TARGET_FILE_DIR:${TARGET}>/vec0.so
      COMMENT "Copying sqlite-vec extension")
endfunction()
