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
# A. External library locations (staged by scripts/build-llama.ps1)
# ===========================================================================
set(_PLAT                 "windows")
set(_VECTOREC_EXT         "vec0.dll")
set(_STATIC_EXT           ".a")

# llama.cpp is built ONCE (CPU + CUDA + Vulkan combined) and staged into a
# SINGLE flat directory.  All DLLs (core + every CPU variant + ggml-cuda +
# ggml-vulkan) and all import libs live side by side, sharing one ggml-base ABI
# (required because every backend links the same ggml-base target).
set(_LLAMA_BASE        "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/llamacpp")
set(_LLAMA_CPU         "${_LLAMA_BASE}")
set(_LLAMA_VULKAN      "${_LLAMA_BASE}")
set(_LLAMA_CUDA        "${_LLAMA_BASE}")
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
# The project uses vcpkg manifest mode which installs to a project-local
# vcpkg_installed/ directory.  Check that first, then fall back to the
# global $VCPKG_ROOT/installed/ path.
set(_VCPKG_INSTALLED_LOCAL "${CMAKE_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")
set(_VCPKG_INSTALLED "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}")
if(EXISTS "${_VCPKG_INSTALLED_LOCAL}")
  set(_VCPKG_INSTALLED "${_VCPKG_INSTALLED_LOCAL}")
endif()
if(NOT EXISTS "${_VCPKG_INSTALLED}")
  message(WARNING
    "vcpkg toolchain not active or packages not installed.\n"
    "  Run:  vcpkg install --triplet x64-windows   (from the repo root)\n"
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
# C. llama.cpp libraries (link against the MSVC import libs staged by
#    scripts/build-llama.ps1 from the llama.cpp build tree)
# ===========================================================================
# llama.cpp is built with GGML_BACKEND_DL + GGML_CPU_ALL_VARIANTS:
#   - ggml-base links ggml-cpu-x64 (the baseline CPU kernels) -- import lib needed
#   - each ggml-cpu-<microarch> variant is a MODULE library (it has an import
#     lib, but nothing links it); ggml loads the best match at runtime via
#     LoadLibrary + ggml_backend_score -- deploy only
#   - ggml-rpc / libomp are deployed but never linked
set(LLAMA_LINK_LIBS "")
foreach(_name llama llama-common mtmd ggml ggml-base ggml-cpu-x64)
  set(_lib "${_LLAMA_CPU}/${_name}.lib")
  if(EXISTS "${_lib}")
    list(APPEND LLAMA_LINK_LIBS "${_lib}")
  else()
    message(WARNING "llama.cpp import lib missing: ${_lib} (run build-dependencies.ps1)")
  endif()
endforeach()
if(EXISTS "${_LLAMA_CUDA}/ggml-cuda.dll")
  list(APPEND LLAMA_LINK_LIBS "${_LLAMA_CUDA}/ggml-cuda.lib")
endif()
if(Vulkan_FOUND AND EXISTS "${_LLAMA_VULKAN}/ggml-vulkan.dll")
  list(APPEND LLAMA_LINK_LIBS "${_LLAMA_VULKAN}/ggml-vulkan.lib")
endif()

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

  # --- Bundled model tree (Embedders/ + Summarizers/) ---
  # seedBundledModels() copies this from <exe>/Resources/Models/ into the user's
  # AppData Models dir on first run (copy-if-missing).  copy_directory keeps
  # rebuilds cheap; the .gguf payloads are large but unchanged.
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Resources/Models
          $<TARGET_FILE_DIR:${TARGET}>/Resources/Models
      COMMENT "Copying bundled model tree")

  # --- Translations: the app loads them from <exe>/i18n/.  Use the .qm list
  #     from qt_add_translations (as Linux/macOS do): a configure-time
  #     file(GLOB) finds nothing in a fresh build dir, since the .qm files are
  #     only generated during the build. ---
  if(HODEIA_QM_FILES)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${TARGET}>/i18n
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${HODEIA_QM_FILES} $<TARGET_FILE_DIR:${TARGET}>/i18n
        COMMENT "Copying translations (.qm)")
  endif()

  # --- llama.cpp: every core DLL + all CPU variants + backend + libomp ---
  # The per-microarch ggml-cpu-* DLLs must sit next to the exe so ggml can
  # LoadLibrary the best one for the host CPU.  Copy the whole set.
  # All backends now live in the single flat ${_LLAMA_BASE} dir, so a few
  # patterns overlap (e.g. ggml-cuda.dll is matched by both ggml*.dll and the
  # explicit entry) — dedupe to avoid redundant copy entries.
  file(GLOB _LLAMA_DEPLOY_DLLS
    "${_LLAMA_BASE}/llama*.dll"
    "${_LLAMA_BASE}/mtmd.dll"
    "${_LLAMA_BASE}/ggml*.dll"
    "${_LLAMA_BASE}/libomp.dll")
  list(REMOVE_DUPLICATES _LLAMA_DEPLOY_DLLS)
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_LLAMA_DEPLOY_DLLS}
          $<TARGET_FILE_DIR:${TARGET}>
      COMMENT "Copying llama.cpp DLLs (CPU/Vulkan/CUDA + libomp)")

  # --- CUDA runtime (cudart + cublas from the CUDA toolkit) ---
  if(EXISTS "${_LLAMA_CUDA}/ggml-cuda.dll")
    set(_CUDA_TOOLKIT "")
    if(DEFINED ENV{CUDA_PATH})
      set(_CUDA_TOOLKIT "$ENV{CUDA_PATH}")
    elseif(EXISTS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.4/bin/x64/cudart64_13.dll")
      set(_CUDA_TOOLKIT "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.4")
    elseif(EXISTS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4/bin/cudart64_12.dll")
      set(_CUDA_TOOLKIT "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4")
    endif()
    if(_CUDA_TOOLKIT)
      # CUDA 13.x moved runtime DLLs to bin/x64/ (and bin/arm64/);
      # CUDA <=12 keeps them in bin/.  Search both.
      file(GLOB _CUDA_RUNTIME_DLLS
        "${_CUDA_TOOLKIT}/bin/cudart64_*.dll"
        "${_CUDA_TOOLKIT}/bin/x64/cudart64_*.dll"
        "${_CUDA_TOOLKIT}/bin/x64/cublas64_*.dll"
        "${_CUDA_TOOLKIT}/bin/x64/cublasLt64_*.dll"
        "${_CUDA_TOOLKIT}/bin/cublas64_*.dll"
        "${_CUDA_TOOLKIT}/bin/cublasLt64_*.dll"
        "${_LLAMA_CUDA}/cublas64_*.dll"
        "${_LLAMA_CUDA}/cublasLt64_*.dll")
      list(REMOVE_DUPLICATES _CUDA_RUNTIME_DLLS)
      if(_CUDA_RUNTIME_DLLS)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_CUDA_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:${TARGET}>
            COMMENT "Copying CUDA runtime DLLs")
      endif()
    endif()
  endif()

  # --- Vulkan loader runtime (from the Vulkan SDK) ---
  set(_VULKAN_SDK_DIR "")
  if(DEFINED ENV{VULKAN_SDK})
    set(_VULKAN_SDK_DIR "$ENV{VULKAN_SDK}")
  elseif(EXISTS "C:/VulkanSDK/1.4.363.0/Bin/vulkan-1.dll")
    set(_VULKAN_SDK_DIR "C:/VulkanSDK/1.4.363.0")
  endif()
  if(Vulkan_FOUND)
    set(_VULKAN_LOADER_DLL "${_VCPKG_INSTALLED}/bin/vulkan-1.dll")
    if(EXISTS "${_VULKAN_SDK_DIR}/Bin/vulkan-1.dll")
      set(_VULKAN_LOADER_DLL "${_VULKAN_SDK_DIR}/Bin/vulkan-1.dll")
    elseif(NOT EXISTS "${_VULKAN_LOADER_DLL}")
      set(_VULKAN_LOADER_DLL "")
    endif()
    if(_VULKAN_LOADER_DLL)
      add_custom_command(TARGET ${TARGET} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_VULKAN_LOADER_DLL}" $<TARGET_FILE_DIR:${TARGET}>/vulkan-1.dll
          COMMENT "Copying vulkan-1.dll")
    else()
      message(WARNING "vulkan-1.dll not found in SDK or vcpkg bin")
    endif()
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
