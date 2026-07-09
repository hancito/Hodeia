cmake_minimum_required(VERSION 3.25)

project(Paperize_CPP VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD         23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
# 2. GPU backend discovery
# ---------------------------------------------------------------------------
# Probe system libraries (Linux only).  Results drive backend auto-selection.

option(LLAMA_FORCE_BACKEND "" "Override backend: cpu | cuda | vulkan | all")

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

# --- CUDA runtime libs (libcuda = driver, libcudart + libcublas = toolkit) ---
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
endif()

# --- Vulkan ---
set(VULKAN_FOUND OFF)
if(PAPERIZE_PLATFORM_LINUX)
  find_library(HOST_VULKAN vulkan PATHS /usr/lib /usr/lib/x86_64-linux-gnu NO_DEFAULT_PATH)
  if(HOST_VULKAN)
    set(VULKAN_FOUND ON)
    message(STATUS "Vulkan found: ${HOST_VULKAN}")
  endif()
endif()

# --- Pick the backend -------------------------------------------------------
# Priority: CUDA > Vulkan > CPU.
# Override with: cmake -DLLAMA_FORCE_BACKEND=cpu

set(LLAMA_BACKEND "")

if(LLAMA_FORCE_BACKEND)
  string(TOLOWER "${LLAMA_FORCE_BACKEND}" LLAMA_BACKEND)
  if(NOT LLAMA_BACKEND STREQUAL "cpu" AND NOT LLAMA_BACKEND STREQUAL "cuda" AND NOT LLAMA_BACKEND STREQUAL "vulkan" AND NOT LLAMA_BACKEND STREQUAL "all")
    message(FATAL_ERROR "LLAMA_FORCE_BACKEND must be cpu, cuda, vulkan, or all")
  endif()
  if(LLAMA_BACKEND STREQUAL "cuda" AND NOT PAPERIZE_PLATFORM_LINUX)
    message(FATAL_ERROR "CUDA is Linux-only")
  endif()
  if(LLAMA_BACKEND STREQUAL "vulkan" AND NOT PAPERIZE_PLATFORM_LINUX)
    message(FATAL_ERROR "Vulkan is Linux-only")
  endif()
  message(STATUS "Backend (forced): ${LLAMA_BACKEND}")
elseif(PAPERIZE_PLATFORM_LINUX)
  if(CUDA_FOUND)
    set(LLAMA_BACKEND "cuda")
  elseif(VULKAN_FOUND)
    set(LLAMA_BACKEND "vulkan")
  else()
    set(LLAMA_BACKEND "cpu")
  endif()
  message(STATUS "Backend (auto): ${LLAMA_BACKEND}")
else()
  set(LLAMA_BACKEND "cpu")
  message(STATUS "Backend (${CMAKE_SYSTEM_NAME}): cpu")
endif()

# Propagate as compile definitions  →  PAPERIZE_BACKEND_CPU etc.
if(LLAMA_BACKEND STREQUAL "all")
  add_compile_definitions(PAPERIZE_BACKEND_CPU PAPERIZE_BACKEND_CUDA PAPERIZE_BACKEND_VULKAN)
else()
  string(TOUPPER "${LLAMA_BACKEND}" _upper)
  add_compile_definitions("PAPERIZE_BACKEND_${_upper}")
endif()

# ---------------------------------------------------------------------------
# 3. External libraries paths
# ---------------------------------------------------------------------------
set(EXTERNAL      ${CMAKE_SOURCE_DIR}/external)
set(LLAMA_INC     ${EXTERNAL}/include/llamacpp)
set(SQLITEVEC_INC ${EXTERNAL}/include/SqliteVec)
set(MUPDF_LIB     ${EXTERNAL}/lib)
set(LLAMA_BASE    ${EXTERNAL}/lib/llamacpp)
set(GGML_SO_VER   0.15.3)

# ---------------------------------------------------------------------------
# 4. PaperizeEngine — static library for all C++ business logic
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
    headers/ingestionengine.h
    headers/enginemanager.h
    headers/sqlitedb.h
    headers/sqlitevec.h
    headers/localbackend.h
    headers/inferenceengine.h
    headers/remotebackend.h
    headers/summarizer.h
    headers/summarythreadmanager.h
    headers/helpers.h

    # --- sources ---
    src/thememanager.cpp
    src/configmanager.cpp
    src/hardwaredetector.cpp
    src/database.cpp
    src/summary.cpp
    src/query.cpp
    src/summaryproxymodel.cpp
    src/ingestionengine.cpp
    src/enginemanager.cpp
    src/sqlitedb.cpp
    src/sqlitevec.cpp
    src/localbackend.cpp
    src/inferenceengine.cpp
    src/remotebackend.cpp
    src/summarizer.cpp
    src/summarythreadmanager.cpp
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
    ${SQLITEVEC_INC}
    ${CMAKE_SOURCE_DIR}/src/thrdprty
)

target_link_libraries(PaperizeEngine PRIVATE
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Network
    ${MUPDF_LIB}/libmupdf.a
    ${MUPDF_LIB}/libmupdf-third.a
    ${MUPDF_LIB}/libmupdf-threads.a
    pthread
    dl
    m
)

# --- OpenMP (ggml-cpu) ---
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
  target_link_libraries(PaperizeEngine PRIVATE OpenMP::OpenMP_C)
  message(STATUS "OpenMP found")
endif()

# ---------------------------------------------------------------------------
# 5. llama.cpp shared libraries
# ---------------------------------------------------------------------------
# Always link base libs from the CPU variant. They are functionally identical
# across backends but avoid dragging in unresolved GPU symbols when the
# toolkit isn't installed on the build machine.
set(_LLAMA_LINK_DIR "${LLAMA_BASE}/cpu")

target_link_libraries(PaperizeEngine PRIVATE
    ${_LLAMA_LINK_DIR}/libllama-common.so.0.0.0
    ${_LLAMA_LINK_DIR}/libllama.so.0.0.0
    ${_LLAMA_LINK_DIR}/libmtmd.so.0.0.0
    ${_LLAMA_LINK_DIR}/libggml.so.${GGML_SO_VER}
    ${_LLAMA_LINK_DIR}/libggml-base.so.${GGML_SO_VER}
    ${_LLAMA_LINK_DIR}/libggml-cpu.so.${GGML_SO_VER}
)

# Backend-specific .so (cuda / vulkan)
set(_BACKEND_LIBS "")
if(LLAMA_BACKEND STREQUAL "cuda" OR LLAMA_BACKEND STREQUAL "all")
  list(APPEND _BACKEND_LIBS "${LLAMA_BASE}/cuda/libggml-cuda.so.${GGML_SO_VER}")
endif()
if(LLAMA_BACKEND STREQUAL "vulkan" OR LLAMA_BACKEND STREQUAL "all")
  list(APPEND _BACKEND_LIBS "${LLAMA_BASE}/vulkan/libggml-vulkan.so.${GGML_SO_VER}")
endif()

# Link system libs for CUDA / Vulkan
if(LLAMA_BACKEND STREQUAL "cuda" OR LLAMA_BACKEND STREQUAL "all")
  if(CUDA_FOUND)
    target_link_libraries(PaperizeEngine PRIVATE ${HOST_CUDA} ${HOST_CUDART} ${HOST_CUBLAS})
    if(HOST_NCCL)
      target_link_libraries(PaperizeEngine PRIVATE ${HOST_NCCL})
    endif()
  else()
    message(WARNING "CUDA backend selected but CUDA toolkit not found on this system.\n"
                    "  libcuda:   ${HOST_CUDA}\n"
                    "  libcudart: ${HOST_CUDART}\n"
                    "  libcublas: ${HOST_CUBLAS}\n"
                    "The CUDA .so will still be shipped and works on machines\n"
                    "with the NVIDIA driver + CUDA toolkit installed.")
  endif()
endif()

if(LLAMA_BACKEND STREQUAL "vulkan" OR LLAMA_BACKEND STREQUAL "all")
  if(VULKAN_FOUND)
    target_link_libraries(PaperizeEngine PRIVATE ${HOST_VULKAN})
  else()
    message(WARNING "Vulkan backend selected but libvulkan not found.")
  endif()
endif()

# ---------------------------------------------------------------------------
# 6. Main executable + QML module
# ---------------------------------------------------------------------------
qt_add_executable(Paperize src/main.cpp)

set_source_files_properties(ui/Colors.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(ui/Fonts.qml  PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(Paperize
    URI "Paperize_CPP"
    QML_FILES
        ui/Colors.qml
        ui/Fonts.qml
        ui/Main.qml
        ui/MyMenuBar.qml
        ui/LeftPane.qml
        ui/CentralPane.qml
        ui/RightPane.qml
        ui/RagWindow.qml
        ui/RagDbPane.qml
        ui/QueryPane.qml
        ui/BackendWindow.qml
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
)

# ---------------------------------------------------------------------------
# 7. Target properties
# ---------------------------------------------------------------------------
set_target_properties(Paperize PROPERTIES
    MACOSX_BUNDLE                 TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION  ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    WIN32_EXECUTABLE              TRUE
    BUILD_RPATH                   "$ORIGIN"
    INSTALL_RPATH                 "$ORIGIN"
)

# ---------------------------------------------------------------------------
# 8. POST_BUILD — copy assets + .so deployment  (Linux)
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

  # --- Base .so ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_LLAMA_LINK_DIR}/libllama.so.0.0.0
          ${_LLAMA_LINK_DIR}/libllama-common.so.0.0.0
          ${_LLAMA_LINK_DIR}/libmtmd.so.0.0.0
          ${_LLAMA_LINK_DIR}/libggml.so.${GGML_SO_VER}
          ${_LLAMA_LINK_DIR}/libggml-base.so.${GGML_SO_VER}
          ${_LLAMA_LINK_DIR}/libggml-cpu.so.${GGML_SO_VER}
          $<TARGET_FILE_DIR:Paperize>
      COMMENT "Copying llama.cpp base .so")

  # --- Backend .so ---
  if(_BACKEND_LIBS)
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_BACKEND_LIBS}
            $<TARGET_FILE_DIR:Paperize>
        COMMENT "Copying llama.cpp backend .so (${LLAMA_BACKEND})")
  endif()

  # --- Base symlinks ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E create_symlink libllama.so.0.0.0                $<TARGET_FILE_DIR:Paperize>/libllama.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink libllama.so.0.0.0                $<TARGET_FILE_DIR:Paperize>/libllama.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink libggml.so.${GGML_SO_VER}        $<TARGET_FILE_DIR:Paperize>/libggml.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink libggml.so.${GGML_SO_VER}        $<TARGET_FILE_DIR:Paperize>/libggml.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink libggml-base.so.${GGML_SO_VER}   $<TARGET_FILE_DIR:Paperize>/libggml-base.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink libggml-base.so.${GGML_SO_VER}   $<TARGET_FILE_DIR:Paperize>/libggml-base.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink libggml-cpu.so.${GGML_SO_VER}    $<TARGET_FILE_DIR:Paperize>/libggml-cpu.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink libggml-cpu.so.${GGML_SO_VER}    $<TARGET_FILE_DIR:Paperize>/libggml-cpu.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink libmtmd.so.0.0.0                 $<TARGET_FILE_DIR:Paperize>/libmtmd.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink libmtmd.so.0.0.0                 $<TARGET_FILE_DIR:Paperize>/libmtmd.so
      COMMAND ${CMAKE_COMMAND} -E create_symlink libllama-common.so.0.0.0         $<TARGET_FILE_DIR:Paperize>/libllama-common.so.0
      COMMAND ${CMAKE_COMMAND} -E create_symlink libllama-common.so.0.0.0         $<TARGET_FILE_DIR:Paperize>/libllama-common.so
      COMMENT "llama.cpp base symlinks")

  # --- Backend symlinks ---
  if(LLAMA_BACKEND STREQUAL "cuda" OR LLAMA_BACKEND STREQUAL "all")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink libggml-cuda.so.${GGML_SO_VER}  $<TARGET_FILE_DIR:Paperize>/libggml-cuda.so.0
        COMMAND ${CMAKE_COMMAND} -E create_symlink libggml-cuda.so.${GGML_SO_VER}  $<TARGET_FILE_DIR:Paperize>/libggml-cuda.so
        COMMENT "libggml-cuda symlinks")
  endif()
  if(LLAMA_BACKEND STREQUAL "vulkan" OR LLAMA_BACKEND STREQUAL "all")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink libggml-vulkan.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml-vulkan.so.0
        COMMAND ${CMAKE_COMMAND} -E create_symlink libggml-vulkan.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml-vulkan.so
        COMMENT "libggml-vulkan symlinks")
  endif()

  # --- $ORIGIN rpath ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/libllama.so.0.0.0
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/libllama-common.so.0.0.0
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/libmtmd.so.0.0.0
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/libggml.so.${GGML_SO_VER}
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/libggml-base.so.${GGML_SO_VER}
      COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/libggml-cpu.so.${GGML_SO_VER}
      COMMENT "Fixing \$ORIGIN rpath on llama .so")

  if(LLAMA_BACKEND STREQUAL "cuda" OR LLAMA_BACKEND STREQUAL "all")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/libggml-cuda.so.${GGML_SO_VER}
        COMMENT "Fixing \$ORIGIN rpath on libggml-cuda")
  endif()
  if(LLAMA_BACKEND STREQUAL "vulkan" OR LLAMA_BACKEND STREQUAL "all")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND patchelf --set-rpath '$ORIGIN' $<TARGET_FILE_DIR:Paperize>/libggml-vulkan.so.${GGML_SO_VER}
        COMMENT "Fixing \$ORIGIN rpath on libggml-vulkan")
  endif()

  # --- sqlite-vec ---
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/external/lib/sqlitevec/vec0.so
          $<TARGET_FILE_DIR:Paperize>/vec0.so
      COMMENT "Copying sqlite-vec extension")

endif()

# ---------------------------------------------------------------------------
# 9. Install
# ---------------------------------------------------------------------------
install(TARGETS Paperize
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
