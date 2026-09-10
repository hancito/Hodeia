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
set(_SHARED_LLAMA      "libllama.so.0.0.0")
set(_SHARED_LLAMA_COMM "libllama-common.so.0.0.0")
set(_SHARED_MTMD       "libmtmd.so.0.0.0")
set(_SHARED_GGML       "libggml.so.0.16.0")
set(_SHARED_GGML_BASE  "libggml-base.so.0.16.0")
set(_SHARED_GGML_CPU   "libggml-cpu.so.0.16.0")
set(_SHARED_GGML_CUDA  "libggml-cuda.so.0.16.0")
set(_SHARED_GGML_VK    "libggml-vulkan.so.0.16.0")
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

if(LLAMA_BACKEND STREQUAL "cuda")
  set(_LLAMA_LINK_DIR "${_LLAMA_BASE}/cuda")
elseif(LLAMA_BACKEND STREQUAL "vulkan")
  set(_LLAMA_LINK_DIR "${_LLAMA_BASE}/vulkan")
else()
  set(_LLAMA_LINK_DIR "${_LLAMA_BASE}/cpu")
endif()

# ===========================================================================
# B2. llama.cpp backend-specific shared libs
# ===========================================================================
set(_BACKEND_SHARED_LIBS "")
if(LLAMA_BACKEND STREQUAL "cuda" AND _SHARED_GGML_CUDA)
  list(APPEND _BACKEND_SHARED_LIBS "${_LLAMA_BASE}/cuda/${_SHARED_GGML_CUDA}")
endif()
if(LLAMA_BACKEND STREQUAL "vulkan" AND _SHARED_GGML_VK)
  list(APPEND _BACKEND_SHARED_LIBS "${_LLAMA_BASE}/vulkan/${_SHARED_GGML_VK}")
endif()

# --- Link variables consumed by src/CMakeLists.txt ---
set(MUPDF_LIB_TARGETS
    ${_MUPDF_LIB}
    ${_MUPDF_THIRD_LIB}
    ${_MUPDF_THREADS_LIB}
    ${_TESS_LIB}
    ${_LEPTO_LIB})

set(LLAMA_LINK_LIBS
    ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA_COMM}
    ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA}
    ${_LLAMA_LINK_DIR}/${_SHARED_MTMD}
    ${_LLAMA_LINK_DIR}/${_SHARED_GGML}
    ${_LLAMA_LINK_DIR}/${_SHARED_GGML_BASE}
    ${_LLAMA_LINK_DIR}/${_SHARED_GGML_CPU})

if(_BACKEND_SHARED_LIBS)
  list(APPEND LLAMA_LINK_LIBS ${_BACKEND_SHARED_LIBS})
endif()

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

  # --- Base .so ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA}
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA_COMM}
          ${_LLAMA_LINK_DIR}/${_SHARED_MTMD}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_BASE}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_CPU}
          $<TARGET_FILE_DIR:${TARGET}>
      COMMENT "Copying llama.cpp base .so")

  # --- Backend .so ---
  if(_BACKEND_SHARED_LIBS)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_BACKEND_SHARED_LIBS}
            $<TARGET_FILE_DIR:${TARGET}>
        COMMENT "Copying llama.cpp backend .so (${LLAMA_BACKEND})")
  endif()

  # --- Base symlinks ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_LLAMA}                $<TARGET_FILE_DIR:${TARGET}>/libllama.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_LLAMA}                $<TARGET_FILE_DIR:${TARGET}>/libllama.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML}                 $<TARGET_FILE_DIR:${TARGET}>/libggml.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML}                 $<TARGET_FILE_DIR:${TARGET}>/libggml.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_BASE}            $<TARGET_FILE_DIR:${TARGET}>/libggml-base.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_BASE}            $<TARGET_FILE_DIR:${TARGET}>/libggml-base.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_CPU}             $<TARGET_FILE_DIR:${TARGET}>/libggml-cpu.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_CPU}             $<TARGET_FILE_DIR:${TARGET}>/libggml-cpu.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_MTMD}                 $<TARGET_FILE_DIR:${TARGET}>/libmtmd.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_MTMD}                 $<TARGET_FILE_DIR:${TARGET}>/libmtmd.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_LLAMA_COMM}           $<TARGET_FILE_DIR:${TARGET}>/libllama-common.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_LLAMA_COMM}           $<TARGET_FILE_DIR:${TARGET}>/libllama-common.so
      COMMENT "llama.cpp base symlinks")

  # --- Backend symlinks ---
  if(LLAMA_BACKEND STREQUAL "cuda")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_CUDA}          $<TARGET_FILE_DIR:${TARGET}>/libggml-cuda.so.0
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_CUDA}          $<TARGET_FILE_DIR:${TARGET}>/libggml-cuda.so
        COMMENT "libggml-cuda symlinks")
  endif()
  if(LLAMA_BACKEND STREQUAL "vulkan")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_VK}            $<TARGET_FILE_DIR:${TARGET}>/libggml-vulkan.so.0
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_VK}            $<TARGET_FILE_DIR:${TARGET}>/libggml-vulkan.so
        COMMENT "libggml-vulkan symlinks")
  endif()

  # --- $ORIGIN rpath (patchelf) ---
  # Locate patchelf so CMake emits it as an absolute path (a bare "patchelf"
  # may be dropped by the generator); the .so already carry an empty RUNPATH
  # that patchelf --set-rpath replaces in place.
  find_program(PATCHELF_EXECUTABLE patchelf REQUIRED)
  # NOTE: the rpath value is "$$ORIGIN" — the doubled dollar is required so the
  # build system's shell does not expand $ORIGIN to the (unset) env var and
  # patch an empty rpath.  The .so already carry an empty RUNPATH that
  # --set-rpath replaces in place.
  set(_PATCH_RPATH
      COMMAND ${PATCHELF_EXECUTABLE} --set-rpath '$$ORIGIN' $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_LLAMA}
      COMMAND ${PATCHELF_EXECUTABLE} --set-rpath '$$ORIGIN' $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_LLAMA_COMM}
      COMMAND ${PATCHELF_EXECUTABLE} --set-rpath '$$ORIGIN' $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_MTMD}
      COMMAND ${PATCHELF_EXECUTABLE} --set-rpath '$$ORIGIN' $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_GGML}
      COMMAND ${PATCHELF_EXECUTABLE} --set-rpath '$$ORIGIN' $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_GGML_BASE}
      COMMAND ${PATCHELF_EXECUTABLE} --set-rpath '$$ORIGIN' $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_GGML_CPU})
  if(LLAMA_BACKEND STREQUAL "cuda")
    list(APPEND _PATCH_RPATH
        COMMAND ${PATCHELF_EXECUTABLE} --set-rpath '$$ORIGIN' $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_GGML_CUDA})
  endif()
  if(LLAMA_BACKEND STREQUAL "vulkan")
    list(APPEND _PATCH_RPATH
        COMMAND ${PATCHELF_EXECUTABLE} --set-rpath '$$ORIGIN' $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_GGML_VK})
  endif()
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
