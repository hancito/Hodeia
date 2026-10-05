# ---------------------------------------------------------------------------
# PlatformWindows.cmake
# Native Windows build with Visual Studio (MSVC).
#
#   * Compiler: MSVC (cl.exe), Release, /MD (dynamic CRT) — matches the
#     pre-built MSVC llama.cpp DLLs.
#   * Qt6: the MSVC build from the Qt installer (found via CMAKE_PREFIX_PATH).
#     NOT the MSYS2 / mingw-w64 build.
#   * MuPDF / Tesseract / Leptonica / Vulkan: provided by vcpkg (x64-windows
#     triplet).  Configure with:
#         cmake -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
#     See vcpkg.json at the repo root for the pinned packages.
#       - libmupdf     → static .lib on Windows (no MuPDF DLL to ship).
#       - tesseract    → dynamic; depends on leptonica + image codecs.
#       - vulkan-loader→ dynamic; provides vulkan-1.dll.
#   * llama.cpp: pre-built MSVC DLLs shipped in-tree under
#     external/lib/windows/llamacpp/ (all three backends: CPU + Vulkan + CUDA).
#     They are linked via import libraries generated at configure time with
#     llvm-dlltool (part of the MSVC LLVM toolset).  ggml auto-selects the CPU
#     variant and the backend at runtime via ggml_backend_load_all().
#
# The top-level CMakeLists.txt includes this file AFTER HODEIA_PLATFORM_WIN=ON
# and BEFORE find_package(Qt6), so this file must not itself call find_package
# on Qt.
# ---------------------------------------------------------------------------

set(HODEIA_PLATFORM_WIN ON)

# ===========================================================================
# A. Shared-library name mapping (in-tree pre-built MSVC DLLs)
# ===========================================================================
set(_PLAT                 "windows")
set(_SHARED_LLAMA         "llama.dll")
set(_SHARED_LLAMA_COMM    "llama-common.dll")
set(_SHARED_MTMD          "mtmd.dll")
set(_SHARED_GGML          "ggml.dll")
set(_SHARED_GGML_BASE     "ggml-base.dll")
set(_SHARED_GGML_CPU_X64  "ggml-cpu-x64.dll")
set(_SHARED_GGML_CPU_SSE  "ggml-cpu-sse42.dll")   # SSE4.2 fallback baseline
set(_SHARED_GGML_CUDA     "ggml-cuda.dll")
set(_SHARED_GGML_VK       "ggml-vulkan.dll")
set(_VECTOREC_EXT         "vec0.dll")
set(_STATIC_EXT           ".a")

# All pre-built llama.cpp DLLs are staged into a single flat dir (see
# scripts/stage-llama.ps1).  Core DLLs + every ggml-cpu-* variant + the
# CUDA/Vulkan backend DLLs + CUDA runtime + libomp.dll all live here.
set(_LLAMA_BASE        "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/llamacpp")
set(_SQLITEVEC_LIB     "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/sqlitevec/${_VECTOREC_EXT}")

# ===========================================================================
# B. vcpkg integration
# ===========================================================================
# The vcpkg toolchain file is passed on the command line:
#   -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
# When it is active, VCPKG_ROOT is defined by the toolchain file.  We locate
# the installed triplet root so the deploy step can ship the runtime DLLs.
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED VCPKG_ROOT)
  set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
endif()
set(VCPKG_TARGET_TRIPLET "x64-windows")
set(_VCPKG_INSTALLED "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}")
if(VCPKG_ROOT AND NOT EXISTS "${_VCPKG_INSTALLED}")
  message(WARNING
    "vcpkg toolchain not active or packages not installed.\n"
    "  Run:  .\\vcpkg\\vcpkg install --triplet x64-windows   (from the repo root)\n"
    "  Expected installed root: ${_VCPKG_INSTALLED}")
endif()

# --- MuPDF (static on Windows → no DLL to ship) -----------------------------
find_package(unofficial-libmupdf QUIET)
if(NOT unofficial-libmupdf_FOUND)
  message(WARNING
    "vcpkg package 'unofficial-libmupdf' not found.\n"
    "  Install with:  vcpkg install libmupdf --triplet x64-windows")
endif()

# --- Tesseract + Leptonica (dynamic) ---------------------------------------
find_package(Tesseract QUIET)
find_package(Leptonica QUIET)
if(NOT Tesseract_FOUND OR NOT Leptonica_FOUND)
  message(WARNING
    "vcpkg packages 'tesseract'/'leptonica' not found.\n"
    "  Install with:  vcpkg install tesseract --triplet x64-windows")
endif()

# --- Vulkan loader (dynamic) -----------------------------------------------
# Optional at link time: if the Vulkan package is missing, the build still
# succeeds and the app runs without the Vulkan backend (ggml-vulkan.dll is
# simply not loaded at runtime).
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
  message(STATUS "Vulkan loader found (vcpkg) → Vulkan::Vulkan")
  # Capture the runtime vulkan-1.dll location for deployment.
  find_file(VULKAN_LOADER_DLL vulkan-1.dll
    PATHS "${_VCPKG_INSTALLED}/bin" NO_DEFAULT_PATH)
else()
  message(STATUS "Vulkan not found — building without the Vulkan backend")
endif()

# ===========================================================================
# C. llama.cpp import libraries (MSVC link.exe needs a .lib per DLL)
# ===========================================================================
# The pre-built release ships DLLs but no import .lib.  Two ways to link them:
#   1. llvm-dlltool (preferred) — generates a real import .lib at configure
#      time.  Part of the MSVC LLVM toolset (VS "LLVM toolchain" component)
#      or installable separately.
#   2. Link the DLL by full path (fallback) — MSVC link.exe resolves the DLL's
#      exports and emits an auto-import library.  Works for the llama.cpp C
#      API (all exports are undecorated C symbols) and needs no extra tools.
# We use (1) when llvm-dlltool is found, else (2).
find_program(LLVM_DLLTOOL_EXECUTABLE
  NAMES llvm-dlltool llvm-dlltool.exe
  HINTS
    "$ENV{VCToolsInstallDir}/llvm/bin"
    "$ENV{VCINSTALLDIR}/Tools/LLVM/x64/bin"
  PATHS
    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/LLVM/x64/bin"
    "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/LLVM/x64/bin"
    "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/LLVM/x64/bin"
    "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/LLVM/x64/bin")

# DLLs that must be *linked* (resolve their C API at build time).  The
# per-microarchitecture ggml-cpu-* variants are NOT linked — ggml loads the
# matching one at runtime via LoadLibrary; they only need to be deployed.
set(_LLAMA_LINK_DLLS
    ${_LLAMA_BASE}/${_SHARED_LLAMA}
    ${_LLAMA_BASE}/${_SHARED_LLAMA_COMM}
    ${_LLAMA_BASE}/${_SHARED_MTMD}
    ${_LLAMA_BASE}/${_SHARED_GGML}
    ${_LLAMA_BASE}/${_SHARED_GGML_BASE}
    ${_LLAMA_BASE}/${_SHARED_GGML_CPU_X64}
    ${_LLAMA_BASE}/${_SHARED_GGML_CPU_SSE})
if(EXISTS "${_LLAMA_BASE}/${_SHARED_GGML_CUDA}")
  list(APPEND _LLAMA_LINK_DLLS "${_LLAMA_BASE}/${_SHARED_GGML_CUDA}")
endif()
if(Vulkan_FOUND AND EXISTS "${_LLAMA_BASE}/${_SHARED_GGML_VK}")
  list(APPEND _LLAMA_LINK_DLLS "${_LLAMA_BASE}/${_SHARED_GGML_VK}")
endif()

set(_LLAMA_LIB_DIR "${CMAKE_BINARY_DIR}/_llama_importlibs")
file(MAKE_DIRECTORY "${_LLAMA_LIB_DIR}")

set(LLAMA_LINK_LIBS "")
foreach(_dll IN LISTS _LLAMA_LINK_DLLS)
  if(NOT EXISTS "${_dll}")
    message(WARNING "llama.cpp DLL missing at configure time: ${_dll}")
    continue()
  endif()
  get_filename_component(_dllname "${_dll}" NAME_WE)
  set(_lib "${_LLAMA_LIB_DIR}/${_dllname}.lib")
  if(LLVM_DLLTOOL_EXECUTABLE)
    # -d: generate the import library from the DLL's export table.
    add_custom_command(
      OUTPUT  "${_lib}"
      COMMAND "${LLVM_DLLTOOL_EXECUTABLE}" -d "${_dll}" -o "${_lib}"
      DEPENDS "${_dll}"
      COMMENT "Generating import lib ${_dllname}.lib from ${_dllname}.dll"
      VERBATIM)
    add_custom_target(_llama_${_dllname}_lib ALL DEPENDS "${_lib}")
    list(APPEND LLAMA_LINK_LIBS "${_lib}")
  else()
    # No dlltool — link the DLL directly.  MSVC link.exe can resolve exports
    # from a DLL path in many cases; this is a best-effort fallback.
    message(STATUS "llvm-dlltool not found — linking ${_dllname}.dll by path")
    list(APPEND LLAMA_LINK_LIBS "${_dll}")
  endif()
endforeach()

# ===========================================================================
# D. System libraries (Win32 / MSVC)
# ===========================================================================
# The app uses Win32/OLE APIs through Qt; these are the standard Win32 libs
# a Qt/Win32 GUI app links against.  No pthread/dl/z on native MSVC.
set(HODEIA_SYSTEM_LIBS
    ole32 user32 gdi32 shell32 shlwapi ws2_32 advapi32 comctl32)

# ===========================================================================
# E. Link variables consumed by src/CMakeLists.txt
# ===========================================================================
# MuPDF is static → a single import target carrying its transitive deps.
if(unofficial-libmupdf_FOUND)
  set(MUPDF_LIB_TARGETS unofficial::libmupdf::libmupdf)
else()
  set(MUPDF_LIB_TARGETS "")
endif()

# Tesseract + Leptonica (dynamic) — linked in the target that uses them.
set(TESS_LEPTO_TARGETS "")
if(Tesseract_FOUND)
  list(APPEND TESS_LEPTO_TARGETS Tesseract::libtesseract)
endif()
if(Leptonica_FOUND)
  list(APPEND TESS_LEPTO_TARGETS leptonica)
endif()

# Vulkan (optional).
if(Vulkan_FOUND)
  set(VULKAN_TARGET Vulkan::Vulkan)
else()
  set(VULKAN_TARGET "")
endif()

# ===========================================================================
# F. POST_BUILD — deployment (requires the Hodeia target to exist)
#    Called from src/CMakeLists.txt after the target is created.
# ===========================================================================
function(hodeia_windows_deploy TARGET)
  # --- Themes, fonts, tessdata (same as Linux) ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/ui/themes $<TARGET_FILE_DIR:${TARGET}>/ui/themes
      COMMENT "Copying themes")
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Fonts $<TARGET_FILE_DIR:${TARGET}>/Fonts
      COMMENT "Copying fonts")
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Resources/tessdata $<TARGET_FILE_DIR:${TARGET}>/tessdata
      COMMENT "Copying tessdata")

  # --- llama.cpp: every core DLL + all CPU variants + backend + libomp ---
  # The per-microarch ggml-cpu-* DLLs must sit next to the exe so ggml can
  # LoadLibrary the best one for the host CPU.  Copy the whole set.
  file(GLOB _LLAMA_DEPLOY_DLLS
    "${_LLAMA_BASE}/llama*.dll"
    "${_LLAMA_BASE}/mtmd.dll"
    "${_LLAMA_BASE}/ggml*.dll"
    "${_LLAMA_BASE}/libomp.dll")
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_LLAMA_DEPLOY_DLLS}
          $<TARGET_FILE_DIR:${TARGET}>
      COMMENT "Copying llama.cpp DLLs (CPU/Vulkan/CUDA + libomp)")

  # --- CUDA 12.4 runtime (only if the CUDA backend DLL was staged) ---
  if(EXISTS "${_LLAMA_BASE}/${_SHARED_GGML_CUDA}")
    file(GLOB _CUDA_RUNTIME_DLLS
      "${_LLAMA_BASE}/cudart64_*.dll"
      "${_LLAMA_BASE}/cublas64_*.dll"
      "${_LLAMA_BASE}/cublasLt64_*.dll")
    if(_CUDA_RUNTIME_DLLS)
      add_custom_command(TARGET ${TARGET} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${_CUDA_RUNTIME_DLLS}
              $<TARGET_FILE_DIR:${TARGET}>
          COMMENT "Copying CUDA runtime DLLs")
    endif()
  endif()

  # --- Vulkan loader runtime (vcpkg vulkan-1.dll) ---
  if(VULKAN_LOADER_DLL AND EXISTS "${VULKAN_LOADER_DLL}")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VULKAN_LOADER_DLL}" $<TARGET_FILE_DIR:${TARGET}>/vulkan-1.dll
        COMMENT "Copying vulkan-1.dll (vcpkg)")
  elseif(Vulkan_FOUND)
    message(WARNING "Vulkan found but vulkan-1.dll not located in ${_VCPKG_INSTALLED}/bin")
  endif()

  # --- Tesseract / Leptonica / image-codec runtime DLLs (vcpkg bin) ---
  # The OCR path is dynamic: tesseract.dll, leptonica.dll and their image
  # codec deps (libpng, libjpeg-turbo, openjpeg, giflib, zlib) must be next
  # to the exe.  Copy a known set from the vcpkg installed bin dir.
  set(_OCR_RUNTIME_DLLS "")
  if(EXISTS "${_VCPKG_INSTALLED}/bin")
    foreach(_n tesseract leptonica libpng16 libjpeg-turbo openjp2-7 gif z)
      find_file(_f NAMES "${_n}.dll" "lib${_n}.dll"
        PATHS "${_VCPKG_INSTALLED}/bin" NO_DEFAULT_PATH)
      if(_f)
        list(APPEND _OCR_RUNTIME_DLLS "${_f}")
      endif()
    endforeach()
    list(REMOVE_DUPLICATES _OCR_RUNTIME_DLLS)
  endif()
  if(_OCR_RUNTIME_DLLS)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_OCR_RUNTIME_DLLS}
            $<TARGET_FILE_DIR:${TARGET}>
        COMMENT "Copying Tesseract/Leptonica runtime DLLs (vcpkg)")
  else()
    message(WARNING "No Tesseract/Leptonica runtime DLLs found in ${_VCPKG_INSTALLED}/bin — OCR may be unavailable at runtime.")
  endif()

  # --- sqlite-vec extension ---
  if(EXISTS "${_SQLITEVEC_LIB}")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_SQLITEVEC_LIB}
            $<TARGET_FILE_DIR:${TARGET}>/vec0.dll
        COMMENT "Copying sqlite-vec extension")
  else()
    message(WARNING "sqlite-vec ${_VECTOREC_EXT} not found at ${_SQLITEVEC_LIB} — RAG vector search will be unavailable.")
  endif()

  # --- Qt runtime (windeployqt) ---
  find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${CMAKE_PREFIX_PATH}" "${Qt6_DIR}/../../bin"
    PATH_SUFFIXES "bin")
  if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${WINDEPLOYQT_EXECUTABLE}
            --dir $<TARGET_FILE_DIR:${TARGET}>
            --no-translations
            $<TARGET_FILE:${TARGET}>
        COMMENT "Running windeployqt (shipping Qt runtime)")
  else()
    message(WARNING "windeployqt not found — Qt DLLs will NOT be shipped.\n"
                    "Set CMAKE_PREFIX_PATH to your MSVC Qt installation prefix.")
  endif()
endfunction()
