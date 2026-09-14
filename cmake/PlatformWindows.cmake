# ---------------------------------------------------------------------------
# PlatformWindows.cmake
# Windows build rules.
#
#   * llama.cpp shared libraries: linked from the in-tree
#     external/lib/windows/llamacpp/<backend>/ (pre-compiled .dll + import
#     libs), exactly like Linux/macOS.
#   * MuPDF / Tesseract / Leptonica: linked against the shared DLLs provided
#     by MSYS2 (mingw64), discovered with find_library (+ pkg-config hints).
#     No in-tree static .a on Windows.
#   * Qt6: optionally taken from MSYS2 too (mingw-w64-qt6-base +
#     mingw-w64-qt6-declarative).  When HODEIA_QT_FROM_MSYS2=ON, find_package
#     resolves Qt6 from the MSYS2 prefix and the Qt runtime DLLs are shipped
#     from MSYS2 as well (windeployqt becomes a fallback only).
#
# Configure MSYS2 location (optional — auto-detected first):
#     cmake -DHODEIA_MINGW_PREFIX=C:/msys64/mingw64 ...
# Use MSYS2's Qt6 instead of a separate Qt installer:
#     cmake -DHODEIA_QT_FROM_MSYS2=ON ...
#
# Expected MSYS2 packages (mingw64):
#     # in the ucrt64 shell:
#     pacman -S mingw-w64-ucrt-x86_64-mupdf mingw-w64-ucrt-x86_64-tesseract-ocr \
#               mingw-w64-ucrt-x86_64-leptonica
#     (plus their transitive deps: libpng, libjpeg-turbo, libtiff, zlib,
#      libwebp, libopenjpeg, freetype, ...)
#     # only if building against MSYS2's Qt:
#     pacman -S mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative \
#               mingw-w64-ucrt-x86_64-qt6-shadertools
#
# Included by the top-level CMakeLists.txt AFTER HODEIA_PLATFORM_WIN=ON.
#
# NOTE: HODEIA_MINGW_PREFIX is resolved HERE (and exported to
# HODEIA_MSYS_PREFIX so the top-level can point find_package(Qt6) at it),
# because this file is included before the Qt6 lookup in the top level.
# ---------------------------------------------------------------------------

set(HODEIA_PLATFORM_WIN ON)

# ===========================================================================
# A. Shared-library name mapping
# ===========================================================================
set(_PLAT              "windows")
set(_SHARED_LLAMA      "llama.dll")
set(_SHARED_LLAMA_COMM "llama-common.dll")
set(_SHARED_MTMD       "mtmd.dll")
set(_SHARED_GGML       "ggml.dll")
set(_SHARED_GGML_BASE  "ggml-base.dll")
set(_SHARED_GGML_CPU   "ggml-cpu.dll")
set(_SHARED_GGML_CUDA  "ggml-cuda.dll")
set(_SHARED_GGML_VK    "ggml-vulkan.dll")
set(_SHARED_GGML_METAL "")
set(_VECTOREC_EXT      "vec0.dll")
set(_STATIC_EXT        ".a")

# llama.cpp base dir (in-tree) + the MSYS2-provided sqlite-vec extension.
set(_LLAMA_BASE        "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/llamacpp")
set(_SQLITEVEC_LIB     "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/sqlitevec/${_VECTOREC_EXT}")

# ===========================================================================
# B. MSYS2 prefix discovery
# ===========================================================================
option(HODEIA_QT_FROM_MSYS2 "Link Qt6 from the MSYS2 prefix (mingw-w64-qt6-*) instead of a separate Qt install" OFF)

# Search order (ucrt64 first — the target ABI; override with
# HODEIA_MINGW_PREFIX or MINGW_PREFIX to use mingw64/clang64):
#   1. HODEIA_MINGW_PREFIX (cache var, e.g. C:/msys64/ucrt64)
#   2. MINGW_PREFIX environment variable
#   3. Common install locations (C:/msys64/{ucrt64,mingw64,clang64})
set(_MSYS_PREFIXES "")
if(DEFINED HODEIA_MINGW_PREFIX)
  list(APPEND _MSYS_PREFIXES "${HODEIA_MINGW_PREFIX}")
endif()
if(DEFINED ENV{MINGW_PREFIX})
  list(APPEND _MSYS_PREFIXES "$ENV{MINGW_PREFIX}")
endif()
foreach(_arch ucrt64 mingw64 clang64)
  list(APPEND _MSYS_PREFIXES "C:/msys64/${_arch}")
endforeach()

# Export the resolved prefix so the top-level can point find_package(Qt6) at
# it when HODEIA_QT_FROM_MSYS2=ON.  Only the first valid (existing) prefix
# is used; the rest remain as search candidates for the libs above.
set(HODEIA_MSYS_PREFIX "")
foreach(_p IN LISTS _MSYS_PREFIXES)
  if(EXISTS "${_p}/lib")
    set(HODEIA_MSYS_PREFIX "${_p}")
    break()
  endif()
endforeach()
if(HODEIA_QT_FROM_MSYS2)
  if(HODEIA_MSYS_PREFIX)
    # Prepend the MSYS2 prefix (and its lib/cmake for the Qt config files).
    list(PREPEND CMAKE_PREFIX_PATH "${HODEIA_MSYS_PREFIX}" "${HODEIA_MSYS_PREFIX}/lib/cmake")
    message(STATUS "Qt6 will be resolved from MSYS2: ${HODEIA_MSYS_PREFIX}")
  else()
    message(WARNING "HODEIA_QT_FROM_MSYS2=ON but no MSYS2 prefix found.\n"
                    "Set -DHODEIA_MINGW_PREFIX=C:/msys64/mingw64 and install\n"
                    "  pacman -S mingw-w64-qt6-base mingw-w64-qt6-declarative mingw-w64-qt6-shadertools")
  endif()
endif()

# Helper: resolve a shared library by its candidate base names under the
# MSYS2 prefixes, falling back to pkg-config for a hint. Emits:
#   <RESULT_VAR> — the resolved import-lib path (or NOTFOUND)
#   <RESULT_INC> — the header include dir (best-effort)
function(msys_find_lib RESULT_VAR)
  cmake_parse_arguments(MF "" "RESULT_INC" "NAMES" ${ARGN})
  set(${RESULT_VAR} "NOTFOUND" PARENT_SCOPE)

  foreach(_base IN LISTS MF_NAMES)
    find_library(_found_lib ${_base}
      PATHS ${_MSYS_PREFIXES}
      PATH_SUFFIXES lib
      NO_DEFAULT_PATH)
    if(_found_lib)
      set(${RESULT_VAR} "${_found_lib}" PARENT_SCOPE)
      get_filename_component(_libdir "${_found_lib}" DIRECTORY)
      get_filename_component(_prefix "${_libdir}" DIRECTORY)
      if(MF_RESULT_INC)
        set(${MF_RESULT_INC} "${_prefix}/include" PARENT_SCOPE)
      endif()
      message(STATUS "  [MSYS2] ${_base} → ${_found_lib}")
      return()
    endif()
  endforeach()

  # pkg-config hint (if a .pc file is installed in the MSYS2 prefix)
  foreach(_p IN LISTS _MSYS_PREFIXES)
    if(EXISTS "${_p}/lib/pkgconfig")
      list(APPEND CMAKE_PREFIX_PATH "${_p}" "${_p}/lib/pkgconfig")
    endif()
  endforeach()
  find_package(PkgConfig QUIET)
  foreach(_base IN LISTS MF_NAMES)
    if(PKG_CONFIG_FOUND AND PKG_CONFIG_EXECUTABLE)
      execute_process(
        COMMAND ${PKG_CONFIG_EXECUTABLE} --exists ${_base}
        RESULT_VARIABLE _pc_ok ERROR_QUIET OUTPUT_QUIET)
      if(_pc_ok EQUAL 0)
        execute_process(
          COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=libdir ${_base}
          OUTPUT_VARIABLE _pc_libdir OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET)
        execute_process(
          COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=includedir ${_base}
          OUTPUT_VARIABLE _pc_inc OUTPUT_STRIP_TRAILING_WHITESPACE
          ERROR_QUIET)
        if(_pc_libdir)
          foreach(_base2 IN LISTS MF_NAMES)
            find_library(_found_lib2 ${_base2}
              PATHS "${_pc_libdir}" NO_DEFAULT_PATH)
            if(_found_lib2)
              set(${RESULT_VAR} "${_found_lib2}" PARENT_SCOPE)
              if(MF_RESULT_INC)
                set(${MF_RESULT_INC} "${_pc_inc}" PARENT_SCOPE)
              endif()
              message(STATUS "  [MSYS2] ${_base} (pkg-config) → ${_found_lib2}")
              return()
            endif()
          endforeach()
        endif()
      endif()
    endif()
  endforeach()
endfunction()

# ===========================================================================
# C. Discover the MSYS2-provided shared libraries
# ===========================================================================
message(STATUS "MSYS2 prefix search: ${_MSYS_PREFIXES}")
set(_MSYS_INC_DIRS "")

# MuPDF (three libraries)
msys_find_lib(WIN_MUPDF_LIB       NAMES mupdf mupdf-x y mupdf-14)
msys_find_lib(WIN_MUPDF_THIRD_LIB NAMES mupdf-third mupdf-third-14)
msys_find_lib(WIN_MUPDF_THREADS_LIB NAMES mupdf-threads mupdf-threads-14)

# Tesseract
msys_find_lib(WIN_TESS_LIB        NAMES tesseract tesseract-5 libtesseract tesseract-ocr)

# Leptonica
msys_find_lib(WIN_LEPTO_LIB       NAMES lept libleptonica leptonica)

# Collect include dirs (best-effort; the mingw64 include dir is the common one)
foreach(_lib ${WIN_MUPDF_LIB} ${WIN_MUPDF_THIRD_LIB} ${WIN_MUPDF_THREADS_LIB}
             ${WIN_TESS_LIB} ${WIN_LEPTO_LIB})
  if(_lib AND NOT _lib STREQUAL "NOTFOUND")
    get_filename_component(_libdir "${_lib}" DIRECTORY)
    get_filename_component(_prefix "${_libdir}" DIRECTORY)
    list(APPEND _MSYS_INC_DIRS "${_prefix}/include")
  endif()
endforeach()
list(REMOVE_DUPLICATES _MSYS_INC_DIRS)

# Warn (do not fail) on any missing library — MSYS2 may not be installed on
# this machine (e.g. cross-configuring).  The build will still link llama.cpp;
# PDF/OCR features will be unavailable at runtime until the DLLs are present.
foreach(_var ${WIN_MUPDF_LIB} ${WIN_MUPDF_THIRD_LIB} ${WIN_MUPDF_THREADS_LIB}
             ${WIN_TESS_LIB} ${WIN_LEPTO_LIB})
  if(NOT DEFINED ${_var} OR ${_var} STREQUAL "NOTFOUND" OR ${_var} STREQUAL "")
    string(TOUPPER "${_var}" _up)
    message(WARNING
      "MSYS2 library '${_var}' not found. PDF/OCR may be unavailable at runtime.\n"
      "  → Install it via:  pacman -S mingw-w64-mupdf mingw-w64-tesseract-ocr mingw-w64-leptonica\n"
      "  → Or set the MSYS2 prefix:  cmake -DHODEIA_MINGW_PREFIX=C:/msys64/mingw64 ...")
  endif()
endforeach()

# Public include dirs for the MSYS2 headers (mupdf/fitz.h, tesseract/baseapi.h).
set(HODEIA_MSYS_INCLUDES "${_MSYS_INC_DIRS}")

# ===========================================================================
# D. GPU backend discovery (CUDA > Vulkan > CPU)
# ===========================================================================
option(LLAMA_FORCE_BACKEND "" "Override backend: cpu | cuda | vulkan")

set(CUDA_FOUND OFF)
if(EXISTS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA")
  file(GLOB _CUDA_VERSIONS LIST_DIRECTORIES ON
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/*")
  list(GET _CUDA_VERSIONS -1 _CUDA_LATEST)
  find_library(HOST_CUDART  cudart  PATHS "${_CUDA_LATEST}/lib/x64" NO_DEFAULT_PATH)
  find_library(HOST_CUBLAS  cublas  PATHS "${_CUDA_LATEST}/lib/x64" NO_DEFAULT_PATH)
  if(HOST_CUDART AND HOST_CUBLAS)
    set(CUDA_FOUND ON)
    message(STATUS "CUDA toolkit found: ${HOST_CUDART}")
  endif()
endif()

set(VULKAN_FOUND OFF)
if(DEFINED ENV{VULKAN_SDK})
  find_library(HOST_VULKAN vulkan-1 PATHS "$ENV{VULKAN_SDK}/lib" NO_DEFAULT_PATH)
endif()
if(HOST_VULKAN)
  set(VULKAN_FOUND ON)
  message(STATUS "Vulkan found: ${HOST_VULKAN}")
endif()

set(METAL_FRAMEWORK "")

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
# E. System libraries
# ===========================================================================
# On Windows the image codecs (png/jpeg/tiff/zlib) come from the MSYS2
# MuPDF-third shared DLL; no pthread/dl needed.  Link `m` for math.
set(HODEIA_SYSTEM_LIBS m)

# OpenMP (ggml-cpu) — libomp.dll must be shipped alongside the executable.
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
  message(STATUS "OpenMP found")
  get_filename_component(_CLANG_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
  set(_OMP_DLL "${_CLANG_BIN}/libomp.dll")
else()
  message(STATUS "OpenMP not found — ggml-cpu will run single-threaded")
endif()

# ===========================================================================
# F. llama.cpp backend-specific shared libs
# ===========================================================================
set(_BACKEND_SHARED_LIBS "")
if(LLAMA_BACKEND STREQUAL "cuda" AND _SHARED_GGML_CUDA)
  list(APPEND _BACKEND_SHARED_LIBS "${_LLAMA_BASE}/cuda/${_SHARED_GGML_CUDA}")
endif()
if(LLAMA_BACKEND STREQUAL "vulkan" AND _SHARED_GGML_VK)
  list(APPEND _BACKEND_SHARED_LIBS "${_LLAMA_BASE}/vulkan/${_SHARED_GGML_VK}")
endif()

# --- Link variables consumed by src/CMakeLists.txt ---
# On Windows MuPDF/Tesseract/Leptonica come from MSYS2 shared DLLs.  Each
# resolved import lib is linked by full path (or skipped with a warning if
# MSYS2 is not installed).  The in-tree static .a are NOT used on Windows.
set(_WIN_THIRD_PARTY_LINK "")
foreach(_var WIN_MUPDF_LIB WIN_MUPDF_THIRD_LIB WIN_MUPDF_THREADS_LIB
             WIN_TESS_LIB WIN_LEPTO_LIB)
  if(DEFINED ${_var} AND NOT ${_var} STREQUAL "NOTFOUND" AND NOT ${_var} STREQUAL "")
    list(APPEND _WIN_THIRD_PARTY_LINK "${${_var}}")
  endif()
endforeach()
set(MUPDF_LIB_TARGETS ${_WIN_THIRD_PARTY_LINK})

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
# G. POST_BUILD — deployment (requires the Hodeia target to exist)
#    Called from src/CMakeLists.txt after the target is created.
# ===========================================================================
function(hodeia_windows_deploy TARGET)
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

  # --- Base .dll ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA}
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA_COMM}
          ${_LLAMA_LINK_DIR}/${_SHARED_MTMD}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_BASE}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_CPU}
          $<TARGET_FILE_DIR:${TARGET}>
      COMMENT "Copying llama.cpp base .dll")

  # --- Backend .dll ---
  if(_BACKEND_SHARED_LIBS)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_BACKEND_SHARED_LIBS}
            $<TARGET_FILE_DIR:${TARGET}>
        COMMENT "Copying llama.cpp backend .dll (${LLAMA_BACKEND})")
  endif()

  # --- MSYS2 runtime DLLs (MuPDF / Tesseract / Leptonica + image codecs) ---
  # On Windows the shared libs are NOT in the in-tree external/lib dir; they
  # live under the MSYS2 prefix.  For each resolved import lib, resolve the
  # actual runtime DLL (lib<base>.dll or <base>.dll) and copy it next to the
  # executable so the app is portable.  Known image-codec deps of MuPDF-third
  # (libpng, libjpeg-turbo, libtiff, zlib, libwebp, libopenjpeg, freetype) are
  # copied too, since the user needs them at runtime.
  set(_MSYS_DLLS_TO_COPY "")
  foreach(_var WIN_MUPDF_LIB WIN_MUPDF_THIRD_LIB WIN_MUPDF_THREADS_LIB
               WIN_TESS_LIB WIN_LEPTO_LIB)
    if(NOT DEFINED ${_var} OR ${_var} STREQUAL "NOTFOUND" OR ${_var} STREQUAL "")
      continue()
    endif()
    get_filename_component(_imp_lib "${${_var}}" NAME_WE)
    get_filename_component(_msys_libdir "${${_var}}" DIRECTORY)
    # Strip a leading "lib" from the import-lib base name.
    string(REGEX REPLACE "^lib" "" _base "${_imp_lib}")
    # Try lib<base>.dll then <base>.dll in the MSYS2 lib dir.
    foreach(_dll_name "lib${_base}.dll" "${_base}.dll")
      if(EXISTS "${_msys_libdir}/${_dll_name}")
        list(APPEND _MSYS_DLLS_TO_COPY "${_msys_libdir}/${_dll_name}")
        break()
      endif()
    endforeach()
  endforeach()
  # Image-codec dependencies of MuPDF-third / Leptonica (common MSYS2 names).
  foreach(_codec libpng16 libpng libjpeg-turbo libjpeg libtiff-6 libtiff
                 zlib1 zlib libwebp libopenjp2-7 libfreetype-6)
    foreach(_msys_libdir IN LISTS _MSYS_PREFIXES)
      set(_msys_libdir "${_msys_libdir}/lib")
      foreach(_dll_name "${_codec}.dll" "lib${_codec}.dll")
        if(EXISTS "${_msys_libdir}/${_dll_name}")
          list(APPEND _MSYS_DLLS_TO_COPY "${_msys_libdir}/${_dll_name}")
          break()
        endif()
      endforeach()
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES _MSYS_DLLS_TO_COPY)
  if(_MSYS_DLLS_TO_COPY)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_MSYS_DLLS_TO_COPY}
            $<TARGET_FILE_DIR:${TARGET}>
        COMMAND_ERROR_IS_FATAL ANY
        COMMENT "Copying MSYS2 runtime DLLs")
  endif()

  # --- sqlite-vec ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_SQLITEVEC_LIB}
          $<TARGET_FILE_DIR:${TARGET}>/vec0.dll
      COMMENT "Copying sqlite-vec extension")

  # --- OpenMP runtime ---
  if(OpenMP_CXX_FOUND AND EXISTS "${_OMP_DLL}")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_OMP_DLL}" $<TARGET_FILE_DIR:${TARGET}>/libomp.dll
        COMMENT "Copying libomp.dll")
  endif()

  # =========================================================================
  # Qt runtime deployment.
  #   * If Qt came from MSYS2 (HODEIA_QT_FROM_MSYS2=ON), the Qt DLLs live in
  #     the MSYS2 bin dir and are copied in the "MSYS2 runtime DLLs" step above
  #     (windeployqt is only used as a fallback for any it misses).
  #   * If Qt came from a separate installer, windeployqt ships the full set.
  # =========================================================================
  if(HODEIA_QT_FROM_MSYS2 AND HODEIA_MSYS_PREFIX AND EXISTS "${HODEIA_MSYS_PREFIX}/bin")
    # Copy the Qt6 DLLs from the MSYS2 bin dir alongside the executable.
    file(GLOB _QT6_DLLS "${HODEIA_MSYS_PREFIX}/bin/Qt6*.dll")
    if(_QT6_DLLS)
      add_custom_command(TARGET ${TARGET} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${_QT6_DLLS}
              $<TARGET_FILE_DIR:${TARGET}>
          COMMAND_ERROR_IS_FATAL ANY
          COMMENT "Copying MSYS2 Qt6 DLLs")
    endif()
  endif()

  find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${CMAKE_PREFIX_PATH}" "${Qt6_DIR}/../../bin"
    PATH_SUFFIXES "bin")

  if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${WINDEPLOYQT_EXECUTABLE}
            --dir $<TARGET_FILE_DIR:${TARGET}>
            --no-system-dsa
            --no-translations
            --no-angle
            --no-opengl-sw
            --qmlimport $<TARGET_PROPERTY:${TARGET},QML_IMPORT_PREFIX>
            $<TARGET_FILE:${TARGET}>
        COMMENT "Running windeployqt (shipping Qt runtime)")
  else()
    message(WARNING "windeployqt not found — Qt DLLs will NOT be shipped.\n"
                    "Set CMAKE_PREFIX_PATH to your Qt installation prefix,\n"
                    "or manually run windeployqt on the built executable.")
  endif()

  # =========================================================================
  # CUDA runtime (optional — only if CUDA backend is selected)
  # =========================================================================
  if(LLAMA_BACKEND STREQUAL "cuda" AND CUDA_FOUND)
    if(EXISTS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA")
      file(GLOB _CUDA_VERSIONS_LIST LIST_DIRECTORIES ON
        "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/*")
      list(GET _CUDA_VERSIONS_LIST -1 _CUDA_LATEST_DIR)
      set(_CUDA_BIN "${_CUDA_LATEST_DIR}/bin")
      add_custom_command(TARGET ${TARGET} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${_CUDA_BIN}/cudart64_*.dll
              ${_CUDA_BIN}/cublas64_*.dll
              ${_CUDA_BIN}/cublasLt64_*.dll
              $<TARGET_FILE_DIR:${TARGET}>
          COMMAND_ERROR_IS_FATAL ANY
          COMMENT "Copying CUDA runtime DLLs")
    endif()
  endif()

  # =========================================================================
  # Vulkan runtime (optional — only if Vulkan backend is selected)
  # =========================================================================
  if(LLAMA_BACKEND STREQUAL "vulkan")
    if(DEFINED ENV{VULKAN_SDK})
      add_custom_command(TARGET ${TARGET} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$ENV{VULKAN_SDK}/Bin/vulkan-1.dll"
              $<TARGET_FILE_DIR:${TARGET}>/vulkan-1.dll
          COMMAND_ERROR_IS_FATAL ANY
          COMMENT "Copying Vulkan runtime DLL")
    else()
      message(WARNING "Vulkan backend selected but VULKAN_SDK not set.\n"
                      "vulkan-1.dll will NOT be shipped.")
    endif()
  endif()
endfunction()
