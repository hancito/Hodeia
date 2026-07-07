cmake_minimum_required(VERSION 3.16)

project(Paperize_CPP VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Concurrent Network Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(Paperize
    src/main.cpp
)

set_source_files_properties(ui/Colors.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set_source_files_properties(ui/Fonts.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(Paperize
    URI Paperize_CPP
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
    SOURCES
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
)

# Add sqlite3.c directly to the target (outside qt_add_qml_module)
# to avoid C++ compiler flags being passed to the C compiler.
set_source_files_properties(src/thrdprty/sqlite3.c PROPERTIES LANGUAGE C)
target_sources(Paperize PRIVATE
    src/thrdprty/sqlite3.c
)

# Add resources separately
qt_add_resources(Paperize "resources" PREFIX "/" FILES
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

# Copy themes folder to build directory
add_custom_command(TARGET Paperize POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/ui/themes
    $<TARGET_FILE_DIR:Paperize>/ui/themes
)

# Copy fonts folder to build directory
add_custom_command(TARGET Paperize POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Fonts
    $<TARGET_FILE_DIR:Paperize>/Fonts
)

set_target_properties(Paperize PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

# ── External libraries ──────────────────────────────────────────────
set(EXTERNAL      ${CMAKE_SOURCE_DIR}/external)
set(LLAMA_INC     ${EXTERNAL}/include/llamacpp)
set(SQLITEVEC_INC ${EXTERNAL}/include/SqliteVec)
set(MUPDF_LIB     ${EXTERNAL}/lib)

target_include_directories(Paperize PRIVATE
    ${CMAKE_SOURCE_DIR}            # for headers/*.h
    ${LLAMA_INC}                   # for #include "llama.h", "common/*.h"
    ${LLAMA_INC}/vendor            # for #include <nlohmann/json_fwd.hpp>
    ${EXTERNAL}/include            # for #include <Sqlite3/sqlite3.h>, <mupdf/fitz.h>
    ${SQLITEVEC_INC}               # for #include "sqlitevec.h"
    ${CMAKE_SOURCE_DIR}/src/thrdprty
)

target_link_libraries(Paperize PRIVATE
    Qt6::Concurrent
    Qt6::Network
    Qt6::Quick
    Qt6::QuickControls2
    ${MUPDF_LIB}/libmupdf.a
    ${MUPDF_LIB}/libmupdf-third.a
    ${MUPDF_LIB}/libmupdf-threads.a
    pthread
    dl
    m
)

# ── llama.cpp shared libraries (ship ALL backend variants) ─────────
# The runtime decides which backend to use via ggml_backend_load_all().
# We ship every .so that exists alongside the binary and let the user's
# code pick CPU / CUDA / Vulkan at runtime.
#
# Override with: cmake -DLLAMA_FORCE_BACKEND=cpu  (or cuda / vulkan / all)
# When set to cpu/cuda/vulkan, only that backend's .so is shipped and the
# compile defines PAPERIZE_BACKEND_<CPU|CUDA|VULKAN> so the C++ code can
# default to that device.
option(LLAMA_FORCE_BACKEND "" "Force a single backend: cpu, cuda, vulkan (default=all)")

set(LLAMA_BASE ${EXTERNAL}/lib/llamacpp)

# Backend-specific libraries that differ per variant.
set(GGML_SO_VER 0.15.3)

# Decide which backends to ship: forced single variant or auto-detect best.
set(SHIP_VARIANTS "")
if(LLAMA_FORCE_BACKEND)
  string(TOLOWER "${LLAMA_FORCE_BACKEND}" _force)
  if(_force STREQUAL "cpu")
    set(SHIP_VARIANTS "cpu")
  elseif(_force STREQUAL "cuda")
    set(SHIP_VARIANTS "cuda")
  elseif(_force STREQUAL "vulkan")
    set(SHIP_VARIANTS "vulkan")
  else()
    message(FATAL_ERROR "LLAMA_FORCE_BACKEND must be cpu, cuda, or vulkan, got: ${LLAMA_FORCE_BACKEND}")
  endif()
else()
  # Auto-detect: pick the highest-priority backend whose libs exist.
  # Priority: CUDA > Vulkan > CPU.
  set(_DETECTED_BACKEND "cpu")
  if(EXISTS "${LLAMA_BASE}/vulkan")
    set(_DETECTED_BACKEND "vulkan")
  endif()
  if(EXISTS "${LLAMA_BASE}/cuda")
    set(_DETECTED_BACKEND "cuda")
  endif()
  set(SHIP_VARIANTS "${_DETECTED_BACKEND}")
  message(STATUS "llama.cpp backend auto-detected: ${_DETECTED_BACKEND}")
endif()
message(STATUS "llama.cpp backend shipped: ${SHIP_VARIANTS}")

# Collect backend-specific .so to ship.
set(BACKEND_LIBS "")
foreach(VARIANT IN LISTS SHIP_VARIANTS)
  if(VARIANT STREQUAL "cpu")
    continue() # cpu has no extra backend .so beyond ggml-cpu
  endif()
  if(EXISTS "${LLAMA_BASE}/${VARIANT}")
    list(APPEND BACKEND_LIBS "${LLAMA_BASE}/${VARIANT}/libggml-${VARIANT}.so.${GGML_SO_VER}")
  endif()
endforeach()

# Define compile flag so C++ knows which backends are available.
foreach(VARIANT IN ITEMS cpu cuda vulkan)
  list(FIND SHIP_VARIANTS "${VARIANT}" _idx)
  if(_idx GREATER -1)
    add_compile_definitions("PAPERIZE_BACKEND_${VARIANT}")
  endif()
endforeach()

# Link: pick the highest-priority available backend for the common libs
# (llama, ggml, mtmd are identical across variants — any works).
set(_LLAMA_LINK_DIR "${LLAMA_BASE}/cpu")
foreach(VARIANT IN ITEMS cuda vulkan cpu)
  if(EXISTS "${LLAMA_BASE}/${VARIANT}")
    set(_LLAMA_LINK_DIR "${LLAMA_BASE}/${VARIANT}")
    break()
  endif()
endforeach()

set(LLAMA_GGML_LIBS
    ${_LLAMA_LINK_DIR}/libllama-common.so.0.0.0
    ${_LLAMA_LINK_DIR}/libllama.so.0.0.0
    ${_LLAMA_LINK_DIR}/libmtmd.so.0.0.0
    ${_LLAMA_LINK_DIR}/libggml.so.${GGML_SO_VER}
    ${_LLAMA_LINK_DIR}/libggml-base.so.${GGML_SO_VER}
    ${_LLAMA_LINK_DIR}/libggml-cpu.so.${GGML_SO_VER}
)

target_link_libraries(Paperize PRIVATE ${LLAMA_GGML_LIBS})

# System libs — only link for shipped variants.
find_library(HOST_VULKAN  vulkan  PATHS /usr/lib /usr/lib/x86_64-linux-gnu NO_DEFAULT_PATH)
find_library(HOST_CUDA    cuda    PATHS /usr/lib/x86_64-linux-gnu /usr/lib NO_DEFAULT_PATH)
find_library(HOST_NCCL    nccl    PATHS /usr/lib/x86_64-linux-gnu /usr/lib /opt/cuda/lib64 NO_DEFAULT_PATH)

list(FIND SHIP_VARIANTS "cuda" _cuda_idx)
if(_cuda_idx GREATER -1)
  if(HOST_CUDA)
    target_link_libraries(Paperize PRIVATE ${HOST_CUDA})
  endif()
  if(HOST_NCCL)
    target_link_libraries(Paperize PRIVATE ${HOST_NCCL})
  endif()
endif()

list(FIND SHIP_VARIANTS "vulkan" _vulkan_idx)
if(_vulkan_idx GREATER -1 AND HOST_VULKAN)
  message(STATUS "Vulkan backend available — linking ${HOST_VULKAN}")
  target_link_libraries(Paperize PRIVATE ${HOST_VULKAN})
endif()

# OpenMP — required by ggml-cpu backend
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
  message(STATUS "OpenMP found")
  target_link_libraries(Paperize PRIVATE OpenMP::OpenMP_C)
endif()

# Copy llama.cpp shared libraries to build directory.
# The base libs (llama, ggml, ggml-base, ggml-cpu, mtmd, llama-common) are
# identical across variants — copy from the first available one.
add_custom_command(TARGET Paperize POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${_LLAMA_LINK_DIR}/libllama.so.0.0.0
        ${_LLAMA_LINK_DIR}/libllama-common.so.0.0.0
        ${_LLAMA_LINK_DIR}/libmtmd.so.0.0.0
        ${_LLAMA_LINK_DIR}/libggml.so.${GGML_SO_VER}
        ${_LLAMA_LINK_DIR}/libggml-base.so.${GGML_SO_VER}
        ${_LLAMA_LINK_DIR}/libggml-cpu.so.${GGML_SO_VER}
        $<TARGET_FILE_DIR:Paperize>
    COMMENT "Copying llama.cpp base .so"
)

# Copy every backend-specific .so (cuda, vulkan) alongside the binary.
# ggml_backend_load_all() discovers them dynamically at runtime.
if(BACKEND_LIBS)
  add_custom_command(TARGET Paperize POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${BACKEND_LIBS}
          $<TARGET_FILE_DIR:Paperize>
      COMMENT "Copying llama.cpp backend .so (${SHIP_VARIANTS})"
  )
endif()

# Copy sqlite-vec extension to build directory
add_custom_command(TARGET Paperize POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/external/lib/sqlitevec/vec0.so
    $<TARGET_FILE_DIR:Paperize>/vec0.so
    COMMENT "Copying sqlite-vec extension"
)

# Recreate symlinks next to the binary so the dynamic linker resolves them.
add_custom_command(TARGET Paperize POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libllama.so.0.0.0 $<TARGET_FILE_DIR:Paperize>/libllama.so.0
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libllama.so.0.0.0 $<TARGET_FILE_DIR:Paperize>/libllama.so
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml.so.0
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml.so
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml-base.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml-base.so.0
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml-base.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml-base.so
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml-cpu.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml-cpu.so.0
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml-cpu.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml-cpu.so
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libmtmd.so.0.0.0 $<TARGET_FILE_DIR:Paperize>/libmtmd.so.0
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libmtmd.so.0.0.0 $<TARGET_FILE_DIR:Paperize>/libmtmd.so
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libllama-common.so.0.0.0 $<TARGET_FILE_DIR:Paperize>/libllama-common.so.0
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libllama-common.so.0.0.0 $<TARGET_FILE_DIR:Paperize>/libllama-common.so
    COMMENT "Creating llama.cpp .so symlinks"
)

# Backend-specific symlinks — create for every shipped variant.
list(FIND SHIP_VARIANTS "cuda" _cuda_idx)
if(_cuda_idx GREATER -1)
  add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libggml-cuda.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml-cuda.so.0
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libggml-cuda.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml-cuda.so
        COMMENT "Creating libggml-cuda symlinks"
    )
endif()
list(FIND SHIP_VARIANTS "vulkan" _vulkan_idx)
if(_vulkan_idx GREATER -1)
  add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libggml-vulkan.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml-vulkan.so.0
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libggml-vulkan.so.${GGML_SO_VER} $<TARGET_FILE_DIR:Paperize>/libggml-vulkan.so
        COMMENT "Creating libggml-vulkan symlinks"
    )
endif()

# Fix rpath of all real .so so they find each other via $ORIGIN.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fix_llama_rpath.sh"
    "#!/bin/bash\n"
    "DIR=\"$1\"\n"
    "for f in \"$DIR\"/libllama.so.0.0.0 \"$DIR\"/libggml.so.${GGML_SO_VER}"
    " \"$DIR\"/libggml-base.so.${GGML_SO_VER} \"$DIR\"/libggml-cpu.so.${GGML_SO_VER}"
    " \"$DIR\"/libmtmd.so.0.0.0 \"$DIR\"/libllama-common.so.0.0.0"
)
list(FIND SHIP_VARIANTS "cuda" _cuda_idx)
if(_cuda_idx GREATER -1)
  file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/fix_llama_rpath.sh"
        " \"$DIR\"/libggml-cuda.so.${GGML_SO_VER}")
endif()
list(FIND SHIP_VARIANTS "vulkan" _vulkan_idx)
if(_vulkan_idx GREATER -1)
  file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/fix_llama_rpath.sh"
        " \"$DIR\"/libggml-vulkan.so.${GGML_SO_VER}")
endif()
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/fix_llama_rpath.sh"
    "\n  do patchelf --set-rpath '\$ORIGIN' \"$f\"; done\n")

add_custom_command(TARGET Paperize POST_BUILD
    COMMAND bash ${CMAKE_CURRENT_BINARY_DIR}/fix_llama_rpath.sh $<TARGET_FILE_DIR:Paperize>
    COMMENT "Fixing \$ORIGIN rpath on llama .so"
)

install(TARGETS Paperize
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
