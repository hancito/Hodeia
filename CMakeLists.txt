cmake_minimum_required(VERSION 3.25)

project(Paperize VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD         23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Windows: hint CMake where Qt6 lives (adjust if your Qt is elsewhere)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND NOT CMAKE_PREFIX_PATH)
  set(CMAKE_PREFIX_PATH "F:/Qt/6.11.1/msvc2022_64" CACHE STRING "Qt6 prefix" FORCE)
endif()

# Parallel build — use all cores by default (override: -DCMAKE_BUILD_PARALLEL_LEVEL=4)
if(NOT DEFINED CMAKE_BUILD_PARALLEL_LEVEL)
  include(ProcessorCount)
  ProcessorCount(NPROC)
  if(NPROC GREATER 0)
    set(CMAKE_BUILD_PARALLEL_LEVEL ${NPROC} CACHE STRING "Build parallelism" FORCE)
    message(STATUS "Build parallelism: ${NPROC} cores")
  endif()
endif()

# ---------------------------------------------------------------------------
# 1. Qt6 — project setup
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Concurrent Network Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.10)

# ---------------------------------------------------------------------------
# 2. Platform detection
# ---------------------------------------------------------------------------
set(PAPERIZE_PLATFORM_LINUX OFF)
set(PAPERIZE_PLATFORM_WIN   OFF)
set(PAPERIZE_PLATFORM_MAC   OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(PAPERIZE_PLATFORM_LINUX ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(PAPERIZE_PLATFORM_WIN ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(PAPERIZE_PLATFORM_MAC ON)
endif()

# ---------------------------------------------------------------------------
# 3. Per-platform library extension helpers
# ---------------------------------------------------------------------------
# Shared-library name mapping (llama.cpp builds use consistent basenames):
#   Linux:  libllama.so.0.0.0        macOS: libllama.dylib        Windows: llama.dll
# We resolve the *actual* filename from the platform-specific directory.

if(PAPERIZE_PLATFORM_LINUX)
  set(_PLAT               "linux")
  set(_SHARED_LLAMA       "libllama.so.0.0.0")
  set(_SHARED_LLAMA_COMM  "libllama-common.so.0.0.0")
  set(_SHARED_MTMD        "libmtmd.so.0.0.0")
  set(_SHARED_GGML        "libggml.so.0.16.0")
  set(_SHARED_GGML_BASE   "libggml-base.so.0.16.0")
  set(_SHARED_GGML_CPU    "libggml-cpu.so.0.16.0")
  set(_SHARED_GGML_CUDA   "libggml-cuda.so.0.16.0")
  set(_SHARED_GGML_VK     "libggml-vulkan.so.0.16.0")
  set(_SHARED_GGML_METAL  "")
  set(_VECTOREC_EXT       "vec0.so")
  set(_STATIC_EXT         ".a")
elseif(PAPERIZE_PLATFORM_MAC)
  # macOS Apple Silicon — Metal only, no CUDA/Vulkan/choice.
  set(_PLAT               "mac")
  set(_SHARED_LLAMA       "libllama.dylib")
  set(_SHARED_LLAMA_COMM  "libllama-common.dylib")
  set(_SHARED_MTMD        "libmtmd.dylib")
  set(_SHARED_GGML        "libggml.dylib")
  set(_SHARED_GGML_BASE   "libggml-base.dylib")
  set(_SHARED_GGML_CPU    "libggml-cpu.dylib")
  set(_SHARED_GGML_METAL  "libggml-metal.dylib")
  set(_VECTOREC_EXT       "vec0.dylib")
  set(_STATIC_EXT         ".a")
elseif(PAPERIZE_PLATFORM_WIN)
  # Windows: clang-cl produces .lib, MSYS2/MinGW-Clang produces .a.
  # CMake handles both — but we normalise to .a (MSYS2 convention) and
  # fall back to .lib for clang-cl native builds.
  set(_PLAT               "windows")
  set(_SHARED_LLAMA       "llama.dll")
  set(_SHARED_LLAMA_COMM  "llama-common.dll")
  set(_SHARED_MTMD        "mtmd.dll")
  set(_SHARED_GGML        "ggml.dll")
  set(_SHARED_GGML_BASE   "ggml-base.dll")
  set(_SHARED_GGML_CPU    "ggml-cpu.dll")
  set(_SHARED_GGML_CUDA   "ggml-cuda.dll")
  set(_SHARED_GGML_VK     "ggml-vulkan.dll")
  set(_SHARED_GGML_METAL  "")
  set(_VECTOREC_EXT       "vec0.dll")
  set(_STATIC_EXT         ".a")
endif()

# Platform-specific static library paths
set(_MUPDF_LIB          "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf${_STATIC_EXT}")
set(_MUPDF_THIRD_LIB    "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf-third${_STATIC_EXT}")
set(_MUPDF_THREADS_LIB  "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf-threads${_STATIC_EXT}")
set(_TESS_LIB           "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libtesseract${_STATIC_EXT}")
set(_LEPTO_LIB          "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libleptonica${_STATIC_EXT}")
set(_LLAMA_BASE         "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/llamacpp")
set(_SQLITEVEC_LIB      "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/sqlitevec/${_VECTOREC_EXT}")

# ---------------------------------------------------------------------------
# 4. GPU backend discovery + system lib probing
# ---------------------------------------------------------------------------
# Linux: CUDA > Vulkan > CPU (auto or forced via -DLLAMA_FORCE_BACKEND)
# Windows: CUDA > Vulkan > CPU (auto or forced)
# macOS: Metal only — no choice.

option(LLAMA_FORCE_BACKEND "" "Override backend: cpu | cuda | vulkan (Linux/Windows only)")

# --- CUDA ---
set(CUDA_FOUND OFF)
if(PAPERIZE_PLATFORM_LINUX)
  find_library(HOST_CUDA    cuda   PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/cuda/lib64 /opt/cuda/lib64 NO_DEFAULT_PATH)
  find_library(HOST_CUDART  cudart PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/cuda/lib64 /opt/cuda/lib64 NO_DEFAULT_PATH)
  find_library(HOST_CUBLAS  cublas PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/cuda/lib64 /opt/cuda/lib64 NO_DEFAULT_PATH)
  find_library(HOST_NCCL    nccl   PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/cuda/lib64 /opt/cuda/lib64 NO_DEFAULT_PATH)
  if(HOST_CUDA AND HOST_CUDART AND HOST_CUBLAS)
    set(CUDA_FOUND ON)
    message(STATUS "CUDA toolkit found: ${HOST_CUDART}")
  endif()
elseif(PAPERIZE_PLATFORM_WIN)
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
endif()

# --- Vulkan ---
set(VULKAN_FOUND OFF)
if(PAPERIZE_PLATFORM_LINUX)
  find_library(HOST_VULKAN vulkan PATHS /usr/lib /usr/lib/x86_64-linux-gnu NO_DEFAULT_PATH)
  if(HOST_VULKAN)
    set(VULKAN_FOUND ON)
    message(STATUS "Vulkan found: ${HOST_VULKAN}")
  endif()
elseif(PAPERIZE_PLATFORM_WIN)
  if(DEFINED ENV{VULKAN_SDK})
    find_library(HOST_VULKAN vulkan-1 PATHS "$ENV{VULKAN_SDK}/lib" NO_DEFAULT_PATH)
  endif()
  if(HOST_VULKAN)
    set(VULKAN_FOUND ON)
    message(STATUS "Vulkan found: ${HOST_VULKAN}")
  endif()
endif()

# --- Metal (macOS — always present, no detection needed) ---
set(METAL_FRAMEWORK "")
if(PAPERIZE_PLATFORM_MAC)
  find_library(METAL_FRAMEWORK Metal)
  if(METAL_FRAMEWORK)
    message(STATUS "Metal framework found")
  endif()
endif()

# --- Pick the backend -------------------------------------------------------
set(LLAMA_BACKEND "")

if(PAPERIZE_PLATFORM_MAC)
  # macOS: Metal is the only backend. No choice.
  set(LLAMA_BACKEND "metal")
  message(STATUS "Backend (macOS): metal")
elseif(LLAMA_FORCE_BACKEND)
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

# Propagate as compile definitions  →  PAPERIZE_BACKEND_CPU etc.
string(TOUPPER "${LLAMA_BACKEND}" _upper)
add_compile_definitions("PAPERIZE_BACKEND_${_upper}")

# Resolve the llama.cpp link directory for the selected backend
if(LLAMA_BACKEND STREQUAL "cuda")
  set(_LLAMA_LINK_DIR "${_LLAMA_BASE}/cuda")
elseif(LLAMA_BACKEND STREQUAL "vulkan")
  set(_LLAMA_LINK_DIR "${_LLAMA_BASE}/vulkan")
elseif(LLAMA_BACKEND STREQUAL "metal")
  set(_LLAMA_LINK_DIR "${_LLAMA_BASE}/metal")
else()
  set(_LLAMA_LINK_DIR "${_LLAMA_BASE}/cpu")
endif()

# ---------------------------------------------------------------------------
# 5. Common include directories
# ---------------------------------------------------------------------------
set(EXTERNAL      ${CMAKE_SOURCE_DIR}/external)
set(LLAMA_INC     ${EXTERNAL}/include/llamacpp)
set(SQLITEVEC_INC ${EXTERNAL}/include/SqliteVec)
set(MUPDF_INC     ${EXTERNAL}/include/mupdf)
set(TESS_INC      ${EXTERNAL}/include/tesseract)

# ---------------------------------------------------------------------------
# 6. PaperizeEngine — static library for all C++ business logic
# ---------------------------------------------------------------------------
add_library(PaperizeEngine STATIC
    # --- headers ---
    headers/thememanager.h
    headers/configmanager.h
    headers/hardwaredetector.h
    headers/database.h
    headers/summary.h
    headers/query.h
    headers/summaryproxymodel.h
    headers/queryproxymodel.h
    headers/ingestionengine.h
    headers/enginemanager.h
    headers/sqlitedb.h
    headers/sqlitevec.h
    headers/localbackend.h
    headers/inferenceengine.h
    headers/remotebackend.h
    headers/summarizer.h
    headers/summarythreadmanager.h
    headers/queryworker.h
    headers/querycontroller.h
    headers/helpers.h

    # --- sources ---
    src/thememanager.cpp
    src/configmanager.cpp
    src/hardwaredetector.cpp
    src/database.cpp
    src/summary.cpp
    src/query.cpp
    src/summaryproxymodel.cpp
    src/queryproxymodel.cpp
    src/ingestionengine.cpp
    src/enginemanager.cpp
    src/sqlitedb.cpp
    src/sqlitevec.cpp
    src/localbackend.cpp
    src/inferenceengine.cpp
    src/remotebackend.cpp
    src/summarizer.cpp
    src/summarythreadmanager.cpp
    src/queryworker.cpp
    src/querycontroller.cpp
    src/helpers.cpp

    # --- third-party C source (kept as C, not C++) ---
    src/thrdprty/sqlite3.c
)
set_source_files_properties(src/thrdprty/sqlite3.c PROPERTIES LANGUAGE C)

target_include_directories(PaperizeEngine PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${LLAMA_INC}
    ${LLAMA_INC}/vendor
    ${EXTERNAL}/include
    ${MUPDF_INC}
    ${SQLITEVEC_INC}
    ${TESS_INC}
    ${CMAKE_SOURCE_DIR}/src/thrdprty
)

# --- Static third-party libs (platform-aware extensions) ---
target_link_libraries(PaperizeEngine PRIVATE
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Network
    ${_MUPDF_LIB}
    ${_MUPDF_THIRD_LIB}
    ${_MUPDF_THREADS_LIB}
    ${_TESS_LIB}
    ${_LEPTO_LIB}
)

# --- Platform-specific system libs ---
if(PAPERIZE_PLATFORM_LINUX)
  target_link_libraries(PaperizeEngine PRIVATE pthread dl m z png jpeg tiff)
elseif(PAPERIZE_PLATFORM_MAC)
  # MuPDF with MUPDF_BUILD_THIRD_PARTY=ON embeds libpng, libjpeg, libtiff,
  # zlib, freetype, harfbuzz, openjpeg — no extra system libs needed.
  # Leptonica must be built against the same static deps from MuPDF.
  # Tesseract has no extra deps beyond Leptonica.
  #
  # If you cannot build MuPDF with MUPDF_BUILD_THIRD_PARTY=ON, uncomment
  # the find_library calls below and link the corresponding .dylib.
  # You MUST then ship those .dylib alongside the app.
  target_link_libraries(PaperizeEngine PRIVATE
    ${METAL_FRAMEWORK}
    m
  )
elseif(PAPERIZE_PLATFORM_WIN)
  # Windows: PNG, JPEG, TIFF, ZLIB come from MuPDF-third static lib
  # No pthread/dl needed — SQLite uses LoadLibrary on Windows
  target_link_libraries(PaperizeEngine PRIVATE m)
endif()

# --- OpenMP (ggml-cpu) ---
# The OpenMP runtime (libomp.dylib / libomp.dll) is NOT part of the OS on
# macOS or Windows.  We must ship it alongside the executable so the user
# does not need to install anything extra.
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
  target_link_libraries(PaperizeEngine PRIVATE OpenMP::OpenMP_C)
  message(STATUS "OpenMP found")

  # Locate the shared runtime for deployment.
  # OpenMP::OpenMP_C points to the import lib / .a; the actual .dylib/.dll
  # lives next to it or one level up in the LLVM tree.
  get_target_property(_OMP_LIB OpenMP::OpenMP_C LOCATION)
  get_filename_component(_OMP_DIR "${_OMP_LIB}" DIRECTORY)

  if(PAPERIZE_PLATFORM_MAC)
    # libomp.dylib sits beside libomp.a inside the LLVM prefix/lib/
    set(_OMP_DYLIB "${_OMP_DIR}/libomp.dylib")
  elseif(PAPERIZE_PLATFORM_WIN)
    # libomp.dll sits beside libomp.lib inside the LLVM bin/ or lib/ dir.
    # clang-cl puts it in the LLVM bin/ directory.
    set(_OMP_DLL_CANDIDATES
      "${_OMP_DIR}/libomp.dll"
      "${CMAKE_CXX_COMPILER}"  # directory of clang-cl itself → LLVM/bin
    )
    # Resolve: replace clang-cl → parent dir, then look for libomp.dll
    get_filename_component(_CLANG_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_OMP_DLL "${_CLANG_BIN}/libomp.dll")
  endif()
else()
  message(STATUS "OpenMP not found — ggml-cpu will run single-threaded")
endif()

# ---------------------------------------------------------------------------
# 7. llama.cpp shared libraries (platform-aware)
# ---------------------------------------------------------------------------
target_link_libraries(PaperizeEngine PRIVATE
    ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA_COMM}
    ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA}
    ${_LLAMA_LINK_DIR}/${_SHARED_MTMD}
    ${_LLAMA_LINK_DIR}/${_SHARED_GGML}
    ${_LLAMA_LINK_DIR}/${_SHARED_GGML_BASE}
    ${_LLAMA_LINK_DIR}/${_SHARED_GGML_CPU}
)

# Backend-specific shared lib
set(_BACKEND_SHARED_LIBS "")
if(LLAMA_BACKEND STREQUAL "cuda" AND _SHARED_GGML_CUDA)
  list(APPEND _BACKEND_SHARED_LIBS "${_LLAMA_BASE}/cuda/${_SHARED_GGML_CUDA}")
endif()
if(LLAMA_BACKEND STREQUAL "vulkan" AND _SHARED_GGML_VK)
  list(APPEND _BACKEND_SHARED_LIBS "${_LLAMA_BASE}/vulkan/${_SHARED_GGML_VK}")
endif()
if(LLAMA_BACKEND STREQUAL "metal" AND _SHARED_GGML_METAL)
  list(APPEND _BACKEND_SHARED_LIBS "${_LLAMA_BASE}/metal/${_SHARED_GGML_METAL}")
endif()

if(_BACKEND_SHARED_LIBS)
  target_link_libraries(PaperizeEngine PRIVATE ${_BACKEND_SHARED_LIBS})
endif()

# Link system libs for CUDA / Vulkan
if(LLAMA_BACKEND STREQUAL "cuda")
  if(CUDA_FOUND)
    if(PAPERIZE_PLATFORM_LINUX)
      target_link_libraries(PaperizeEngine PRIVATE ${HOST_CUDA} ${HOST_CUDART} ${HOST_CUBLAS})
      if(HOST_NCCL)
        target_link_libraries(PaperizeEngine PRIVATE ${HOST_NCCL})
      endif()
    elseif(PAPERIZE_PLATFORM_WIN)
      target_link_libraries(PaperizeEngine PRIVATE ${HOST_CUDART} ${HOST_CUBLAS})
    endif()
  else()
    message(WARNING "CUDA backend selected but CUDA toolkit not found on this system.\n"
                    "The CUDA shared lib will still be shipped and works on machines\n"
                    "with the NVIDIA driver + CUDA toolkit installed.")
  endif()
endif()

if(LLAMA_BACKEND STREQUAL "vulkan")
  if(VULKAN_FOUND)
    target_link_libraries(PaperizeEngine PRIVATE ${HOST_VULKAN})
  else()
    message(WARNING "Vulkan backend selected but libvulkan not found.")
  endif()
endif()

# ---------------------------------------------------------------------------
# 8. Main executable + QML module
# ---------------------------------------------------------------------------
qt_add_executable(Paperize src/main.cpp)

set_source_files_properties(ui/Colors.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(ui/Fonts.qml  PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(Paperize
    URI "PaperizeCPP"
    QML_FILES
        ui/Colors.qml
        ui/Fonts.qml
        ui/Main.qml
        ui/MyMenuBar.qml
        ui/CentralPane.qml
        ui/RightPane.qml
        ui/RagWindow.qml
        ui/RagDbPane.qml
        ui/QueryPane.qml
        ui/BackendWindow.qml
        ui/MyToolBar.qml
)

# Resources
qt_add_resources(Paperize "resources"
    PREFIX "/"
    FILES
        Resources/ai-svgrepo-com.svg
        Resources/align-justify.svg
        Resources/arrow-right-to-file.svg
        Resources/edit.svg
        Resources/file-export.svg
        Resources/folder-open.svg
        Resources/gear.svg
        Resources/list.svg
        Resources/paperize.ico
        Resources/paperize.png
        Resources/refresh.svg
        Resources/save.svg
        Resources/share-square.svg
        Resources/trash-can.svg
        Resources/circle-check.svg
        Resources/circle-xmark.svg
        Resources/arrow-circle-left.svg
        Resources/arrow-circle-right.svg
        Resources/magnifying-glass-plus.svg
        Resources/magnifying-glass-minus.svg
        Resources/arrow-rotate-back.svg
)

# Link PaperizeEngine + Qt GUI components
target_link_libraries(Paperize PRIVATE
    PaperizeEngine
    Qt6::Quick
    Qt6::QuickControls2
)

target_include_directories(Paperize PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${LLAMA_INC}
    ${EXTERNAL}/include
)

# ---------------------------------------------------------------------------
# 9. Target properties
# ---------------------------------------------------------------------------
set_target_properties(Paperize PROPERTIES
    MACOSX_BUNDLE                 TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION  ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    WIN32_EXECUTABLE              TRUE
    BUILD_RPATH                   "$ORIGIN"
    INSTALL_RPATH                 "$ORIGIN"
)

# On macOS, set the install_name for .dylib to @executable_path
if(PAPERIZE_PLATFORM_MAC)
  set_target_properties(Paperize PROPERTIES
    BUILD_RPATH "@executable_path"
    INSTALL_RPATH "@executable_path"
  )
endif()

# ===========================================================================
# 10. POST_BUILD — copy assets + shared libs (platform-specific)
# ===========================================================================

# ---------------------------------------------------------------------------
# 10a. Linux
# ---------------------------------------------------------------------------
if(PAPERIZE_PLATFORM_LINUX)

  # --- Themes & fonts ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/ui/themes $<TARGET_FILE_DIR:Paperize>/ui/themes
      COMMENT "Copying themes")

  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Fonts $<TARGET_FILE_DIR:Paperize>/Fonts
      COMMENT "Copying fonts")

  # --- Tesseract tessdata ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Resources/tessdata $<TARGET_FILE_DIR:Paperize>/tessdata
      COMMENT "Copying tessdata")

  # --- Base .so ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA}
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA_COMM}
          ${_LLAMA_LINK_DIR}/${_SHARED_MTMD}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_BASE}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_CPU}
          $<TARGET_FILE_DIR:Paperize>
      COMMENT "Copying llama.cpp base .so")

  # --- Backend .so ---
  if(_BACKEND_SHARED_LIBS)
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_BACKEND_SHARED_LIBS}
            $<TARGET_FILE_DIR:Paperize>
        COMMENT "Copying llama.cpp backend .so (${LLAMA_BACKEND})")
  endif()

  # --- Base symlinks ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_LLAMA}                $<TARGET_FILE_DIR:Paperize>/libllama.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_LLAMA}                $<TARGET_FILE_DIR:Paperize>/libllama.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML}                 $<TARGET_FILE_DIR:Paperize>/libggml.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML}                 $<TARGET_FILE_DIR:Paperize>/libggml.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_BASE}            $<TARGET_FILE_DIR:Paperize>/libggml-base.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_BASE}            $<TARGET_FILE_DIR:Paperize>/libggml-base.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_CPU}             $<TARGET_FILE_DIR:Paperize>/libggml-cpu.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_CPU}             $<TARGET_FILE_DIR:Paperize>/libggml-cpu.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_MTMD}                 $<TARGET_FILE_DIR:Paperize>/libmtmd.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_MTMD}                 $<TARGET_FILE_DIR:Paperize>/libmtmd.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_LLAMA_COMM}           $<TARGET_FILE_DIR:Paperize>/libllama-common.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_LLAMA_COMM}           $<TARGET_FILE_DIR:Paperize>/libllama-common.so
      COMMENT "llama.cpp base symlinks")

  # --- Backend symlinks ---
  if(LLAMA_BACKEND STREQUAL "cuda")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_CUDA}          $<TARGET_FILE_DIR:Paperize>/libggml-cuda.so.0
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_CUDA}          $<TARGET_FILE_DIR:Paperize>/libggml-cuda.so
        COMMENT "libggml-cuda symlinks")
  endif()
  if(LLAMA_BACKEND STREQUAL "vulkan")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_VK}            $<TARGET_FILE_DIR:Paperize>/libggml-vulkan.so.0
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${_SHARED_GGML_VK}            $<TARGET_FILE_DIR:Paperize>/libggml-vulkan.so
        COMMENT "libggml-vulkan symlinks")
  endif()

  # --- $ORIGIN rpath (patchelf) ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/${_SHARED_LLAMA}
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/${_SHARED_LLAMA_COMM}
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/${_SHARED_MTMD}
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/${_SHARED_GGML}
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/${_SHARED_GGML_BASE}
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/${_SHARED_GGML_CPU}
      COMMENT "Fixing \$ORIGIN rpath on llama .so")

  if(LLAMA_BACKEND STREQUAL "cuda")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/${_SHARED_GGML_CUDA}
        COMMENT "Fixing \$ORIGIN rpath on libggml-cuda")
  endif()
  if(LLAMA_BACKEND STREQUAL "vulkan")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/${_SHARED_GGML_VK}
        COMMENT "Fixing \$ORIGIN rpath on libggml-vulkan")
  endif()

  # --- sqlite-vec ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_SQLITEVEC_LIB}
          $<TARGET_FILE_DIR:Paperize>/vec0.so
      COMMENT "Copying sqlite-vec extension")

endif()

# ---------------------------------------------------------------------------
# 10b. macOS
# ---------------------------------------------------------------------------
if(PAPERIZE_PLATFORM_MAC)

  # On macOS, the executable lives inside a .app bundle:
  #   Paperize.app/Contents/MacOS/Paperize
  # $<TARGET_FILE_DIR:Paperize> → Contents/MacOS/
  # We copy assets next to the executable.

  # --- Themes & fonts ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/ui/themes $<TARGET_FILE_DIR:Paperize>/ui/themes
      COMMENT "Copying themes")

  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Fonts $<TARGET_FILE_DIR:Paperize>/Fonts
      COMMENT "Copying fonts")

  # --- Tesseract tessdata ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Resources/tessdata $<TARGET_FILE_DIR:Paperize>/tessdata
      COMMENT "Copying tessdata")

  # --- Base .dylib ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA}
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA_COMM}
          ${_LLAMA_LINK_DIR}/${_SHARED_MTMD}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_BASE}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_CPU}
          $<TARGET_FILE_DIR:Paperize>
      COMMENT "Copying llama.cpp base .dylib")

  # --- Backend .dylib (Metal) ---
  if(_BACKEND_SHARED_LIBS)
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_BACKEND_SHARED_LIBS}
            $<TARGET_FILE_DIR:Paperize>
        COMMENT "Copying llama.cpp backend .dylib (${LLAMA_BACKEND})")
  endif()

  # --- Fix @rpath → @executable_path on all .dylib ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND install_name_tool -id @executable_path/${_SHARED_LLAMA}      $<TARGET_FILE_DIR:Paperize>/${_SHARED_LLAMA}
      COMMAND install_name_tool -id @executable_path/${_SHARED_LLAMA_COMM} $<TARGET_FILE_DIR:Paperize>/${_SHARED_LLAMA_COMM}
      COMMAND install_name_tool -id @executable_path/${_SHARED_MTMD}       $<TARGET_FILE_DIR:Paperize>/${_SHARED_MTMD}
      COMMAND install_name_tool -id @executable_path/${_SHARED_GGML}       $<TARGET_FILE_DIR:Paperize>/${_SHARED_GGML}
      COMMAND install_name_tool -id @executable_path/${_SHARED_GGML_BASE}  $<TARGET_FILE_DIR:Paperize>/${_SHARED_GGML_BASE}
      COMMAND install_name_tool -id @executable_path/${_SHARED_GGML_CPU}   $<TARGET_FILE_DIR:Paperize>/${_SHARED_GGML_CPU}
      COMMENT "Setting @executable_path install names on .dylib")

  if(_BACKEND_SHARED_LIBS)
    # Build the install_name_tool commands for each backend lib
    set(_MAC_INSTALL_NAME_CMDS "")
    foreach(_BE_LIB IN LISTS _BACKEND_SHARED_LIBS)
      get_filename_component(_BE_NAME "${_BE_LIB}" NAME)
      list(APPEND _MAC_INSTALL_NAME_CMDS
        COMMAND install_name_tool -id @executable_path/${_BE_NAME} $<TARGET_FILE_DIR:Paperize>/${_BE_NAME}
      )
    endforeach()
    add_custom_command(TARGET Paperize POST_BUILD
        ${_MAC_INSTALL_NAME_CMDS}
        COMMENT "Setting @executable_path on backend .dylib")
  endif()

  # --- sqlite-vec ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_SQLITEVEC_LIB}
          $<TARGET_FILE_DIR:Paperize>/vec0.dylib
      COMMENT "Copying sqlite-vec extension")

  # --- OpenMP runtime ---
  if(OpenMP_CXX_FOUND AND EXISTS "${_OMP_DYLIB}")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_OMP_DYLIB}
            $<TARGET_FILE_DIR:Paperize>/libomp.dylib
        COMMAND install_name_tool -id @executable_path/libomp.dylib
            $<TARGET_FILE_DIR:Paperize>/libomp.dylib
        COMMENT "Copying and fixing libomp.dylib")
  endif()

  # =========================================================================
  # macdeployqt — ships all Qt frameworks, plugins, and resources into the
  # .app bundle so the user gets a standalone, drag-and-drop application.
  # =========================================================================
  find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
    HINTS "${CMAKE_PREFIX_PATH}" "${Qt6_DIR}/../../../bin"
    PATH_SUFFIXES "bin"
  )

  if(MACDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${MACDEPLOYQT_EXECUTABLE}
            $<TARGET_BUNDLE_DIR:Paperize>
            -always-overwrite
        COMMENT "Running macdeployqt (shipping Qt frameworks into .app)")
  else()
    message(WARNING "macdeployqt not found — Qt frameworks will NOT be bundled.\n"
                    "Set CMAKE_PREFIX_PATH to your Qt installation prefix,\n"
                    "or manually run macdeployqt on the built .app.")
  endif()

endif()

# ---------------------------------------------------------------------------
# 10c. Windows
# ---------------------------------------------------------------------------
if(PAPERIZE_PLATFORM_WIN)

  # --- Themes & fonts ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/ui/themes $<TARGET_FILE_DIR:Paperize>/ui/themes
      COMMENT "Copying themes")

  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Fonts $<TARGET_FILE_DIR:Paperize>/Fonts
      COMMENT "Copying fonts")

  # --- Tesseract tessdata ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/Resources/tessdata $<TARGET_FILE_DIR:Paperize>/tessdata
      COMMENT "Copying tessdata")

  # --- Base .dll ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA}
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA_COMM}
          ${_LLAMA_LINK_DIR}/${_SHARED_MTMD}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_BASE}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_CPU}
          $<TARGET_FILE_DIR:Paperize>
      COMMENT "Copying llama.cpp base .dll")

  # --- Backend .dll ---
  if(_BACKEND_SHARED_LIBS)
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_BACKEND_SHARED_LIBS}
            $<TARGET_FILE_DIR:Paperize>
        COMMENT "Copying llama.cpp backend .dll (${LLAMA_BACKEND})")
  endif()

  # --- sqlite-vec ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_SQLITEVEC_LIB}
          $<TARGET_FILE_DIR:Paperize>/vec0.dll
      COMMENT "Copying sqlite-vec extension")

  # --- OpenMP runtime ---
  if(OpenMP_CXX_FOUND AND EXISTS "${_OMP_DLL}")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_OMP_DLL}
            $<TARGET_FILE_DIR:Paperize>/libomp.dll
        COMMENT "Copying libomp.dll")
  endif()

  # =========================================================================
  # windeployqt — ships ALL Qt runtime DLLs required for a standalone app.
  # This copies ~150 MB of Qt platform plugins, ICU data, image formats,
  # and every .dll that Paperize links against.
  #
  # The Qt installer places windeployqt.exe next to the Qt binaries.
  # CMAKE_PREFIX_PATH must point to the Qt installation prefix for this to
  # work, or set WINDEPLOYQT_EXECUTABLE as a cache variable.
  # =========================================================================
  find_program(WINDEPLOYQT_EXECUTABLE windeployqt
    HINTS "${CMAKE_PREFIX_PATH}" "${Qt6_DIR}/../../bin"
    PATH_SUFFIXES "bin"
  )

  if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${WINDEPLOYQT_EXECUTABLE}
            --dir $<TARGET_FILE_DIR:Paperize>
            --no-system-dsa
            --no-translations
            --no-angle
            --no-opengl-sw
            --qmlimport $<TARGET_PROPERTY:Paperize,QML_IMPORT_PREFIX>
            $<TARGET_FILE:Paperize>
        COMMENT "Running windeployqt (shipping Qt runtime)")
  else()
    message(WARNING "windeployqt not found — Qt DLLs will NOT be shipped.\n"
                    "Set CMAKE_PREFIX_PATH to your Qt installation prefix,\n"
                    "or manually run windeployqt on the built executable.")
  endif()

  # =========================================================================
  # Visual C++ Redistributable (MSVCRTxx.dll)
  # clang-cl on Windows links against the MSVC runtime (ucrtbase, vcruntime140).
  # These DLLs are NOT in the LLVM folder.  windeployqt copies them
  # automatically when it finds the Visual Studio installation.
  # If it doesn't, the user needs the VC++ Redist installer from Microsoft.
  # =========================================================================

  # =========================================================================
  # CUDA runtime (optional — only needed if CUDA backend is selected)
  # CUDA .dlls are NOT bundled with the Toolkit.  The safest approach is
  # to copy the minimum set of NVIDIA DLLs alongside the executable.
  # The user must have the NVIDIA driver installed (always the case on a
  # GPU machine), but the CUDA Toolkit DLLs (cudart64_xx.dll, cublas64_xx.dll)
  # are NOT part of the driver.
  # =========================================================================
  if(LLAMA_BACKEND STREQUAL "cuda" AND CUDA_FOUND)
    # cudart and cublas DLLs live next to their .lib files in the CUDA Toolkit
    if(EXISTS "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA")
      file(GLOB _CUDA_VERSIONS_LIST LIST_DIRECTORIES ON
        "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/*")
      list(GET _CUDA_VERSIONS_LIST -1 _CUDA_LATEST_DIR)
      set(_CUDA_BIN "${_CUDA_LATEST_DIR}/bin")

      add_custom_command(TARGET Paperize POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${_CUDA_BIN}/cudart64_*.dll
              ${_CUDA_BIN}/cublas64_*.dll
              ${_CUDA_BIN}/cublasLt64_*.dll
              $<TARGET_FILE_DIR:Paperize>
          COMMAND_ERROR_IS_FATAL ANY
          COMMENT "Copying CUDA runtime DLLs")
    endif()
  endif()

  # =========================================================================
  # Vulkan runtime (optional — only needed if Vulkan backend is selected)
  # vulkan-1.dll is NOT part of the GPU driver on Windows.  The Vulkan SDK
  # ships a standalone loader DLL.  Copy it alongside the executable.
  # =========================================================================
  if(LLAMA_BACKEND STREQUAL "vulkan")
    if(DEFINED ENV{VULKAN_SDK})
      add_custom_command(TARGET Paperize POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "$ENV{VULKAN_SDK}/Bin/vulkan-1.dll"
              $<TARGET_FILE_DIR:Paperize>/vulkan-1.dll
          COMMAND_ERROR_IS_FATAL ANY
          COMMENT "Copying Vulkan runtime DLL")
    else()
      message(WARNING "Vulkan backend selected but VULKAN_SDK not set.\n"
                      "vulkan-1.dll will NOT be shipped.  The user must have\n"
                      "a Vulkan-capable GPU driver (which ships the loader).\n"
                      "Set VULKAN_SDK to the Vulkan SDK path to auto-ship it.")
    endif()
  endif()

  # Note: Windows resolves DLLs in the executable's directory automatically.
  # No symlinks or RPATH needed.

endif()

# ---------------------------------------------------------------------------
# 11. Install
# ---------------------------------------------------------------------------
install(TARGETS Paperize
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
