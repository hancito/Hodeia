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
        headers/database.h
        headers/summary.h
        headers/query.h
        headers/summaryproxymodel.h
        headers/ingestionengine.h
        headers/enginemanager.h
        headers/sqlitedb.h
        headers/sqlitevec.h
        headers/localbackend.h
        headers/remotebackend.h
        # headers/summarizer.h
        # headers/summarythreadmanager.h
        headers/helpers.h
        src/thememanager.cpp
        src/configmanager.cpp
        src/database.cpp
        src/summary.cpp
        src/query.cpp
        src/summaryproxymodel.cpp
        src/ingestionengine.cpp
        src/enginemanager.cpp
        src/sqlitedb.cpp
        src/sqlitevec.cpp
        src/localbackend.cpp
        src/remotebackend.cpp
        # src/summarizer.cpp
        # src/summarythreadmanager.cpp
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
set(SQLITEVEC_LIB ${EXTERNAL}/lib/sqlitevec)

target_include_directories(Paperize PRIVATE
    ${CMAKE_SOURCE_DIR}            # for headers/*.h
    ${LLAMA_INC}                   # for #include "llama.h"
    ${EXTERNAL}/include            # for #include <Sqlite3/sqlite3.h>, <mupdf/fitz.h>
    ${SQLITEVEC_INC}               # for #include "sqlitevec.h"
    ${CMAKE_SOURCE_DIR}/src/thrdprty
)

# ── llama.cpp shared library selection (cuda > vulkan > cpu) ────────
set(LLAMA_BASE ${EXTERNAL}/lib/llamacpp)

# Detect CUDA toolkit on the host
find_library(HOST_CUDART  cudart  PATHS /opt/cuda/lib64 /usr/local/cuda/lib64 NO_DEFAULT_PATH)
find_library(HOST_CUBLAS  cublas  PATHS /opt/cuda/lib64 /usr/local/cuda/lib64 NO_DEFAULT_PATH)
find_library(HOST_VULKAN  vulkan  PATHS /usr/lib /usr/lib/x86_64-linux-gnu NO_DEFAULT_PATH)

if(HOST_CUDART AND HOST_CUBLAS AND EXISTS "${LLAMA_BASE}/cuda")
    set(LLAMA_VARIANT cuda)
elseif(HOST_VULKAN AND EXISTS "${LLAMA_BASE}/vulkan")
    set(LLAMA_VARIANT vulkan)
else()
    set(LLAMA_VARIANT cpu)
endif()
message(STATUS "llama.cpp variant: ${LLAMA_VARIANT}")

set(LLAMA_DIR "${LLAMA_BASE}/${LLAMA_VARIANT}")

# Only real (non-symlink) .so files — the linker follows SONAME from these
file(GLOB LLAMA_REAL_SO "${LLAMA_DIR}/lib*.so.*.*")
# Also grab any backend-specific real .so with the right version pattern
if(LLAMA_VARIANT STREQUAL "cuda")
    list(APPEND LLAMA_REAL_SO "${LLAMA_DIR}/libggml-cuda.so.0.15.2")
elseif(LLAMA_VARIANT STREQUAL "vulkan")
    list(APPEND LLAMA_REAL_SO "${LLAMA_DIR}/libggml-vulkan.so.0.15.2")
endif()

# Link against the real libllama (fully versioned) — SONAME carries the .so.0 name
target_link_libraries(Paperize PRIVATE
    ${LLAMA_DIR}/libllama.so.0.0.0
)
target_link_directories(Paperize PRIVATE ${LLAMA_DIR})

# ── Link libraries (order: Qt, MuPDF, llama above) ─────────────────
target_link_libraries(Paperize PRIVATE
    Qt6::Concurrent
    Qt6::Network
    Qt6::Quick
    Qt6::QuickControls2

    # MuPDF (PDF parsing)
    ${MUPDF_LIB}/libmupdf.a
    ${MUPDF_LIB}/libmupdf-third.a
    ${MUPDF_LIB}/libmupdf-threads.a

    # System libraries
    pthread
    dl
    m
)

# OpenMP — required by ggml-cpu backend (all variants)
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found")
    target_link_libraries(Paperize PRIVATE OpenMP::OpenMP_C)
endif()

# CUDA syslibs — required at runtime by the CUDA variant
if(LLAMA_VARIANT STREQUAL "cuda")
    find_library(CUDA_LIB  cuda      PATHS /usr/lib/x86_64-linux-gnu /usr/lib NO_DEFAULT_PATH)
    find_library(NCCL_LIB  nccl      PATHS /usr/lib/x86_64-linux-gnu /usr/lib /opt/cuda/lib64 NO_DEFAULT_PATH)
    if(CUDA_LIB)
        target_link_libraries(Paperize PRIVATE ${CUDA_LIB})
    endif()
    if(NCCL_LIB)
        target_link_libraries(Paperize PRIVATE ${NCCL_LIB})
    endif()
endif()

# Vulkan syslib — required at runtime by the Vulkan variant
if(LLAMA_VARIANT STREQUAL "vulkan")
    if(HOST_VULKAN)
        message(STATUS "Vulkan found — linking ${HOST_VULKAN}")
        target_link_libraries(Paperize PRIVATE ${HOST_VULKAN})
    endif()
endif()

# Copy sqlite-vec extension to build directory
add_custom_command(TARGET Paperize POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/external/lib/sqlitevec/vec0.so
    $<TARGET_FILE_DIR:Paperize>/vec0.so
    COMMENT "Copying sqlite-vec extension"
)

# Copy llama.cpp real (non-symlink) shared libraries to build directory
add_custom_command(TARGET Paperize POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${LLAMA_REAL_SO}
    $<TARGET_FILE_DIR:Paperize>
    COMMENT "Copying llama.cpp ${LLAMA_VARIANT} .so"
)

# Recreate symlinks next to the binary so the dynamic linker resolves them
# libNAME.so -> libNAME.so.0 -> libNAME.so.0.X.Y
add_custom_command(TARGET Paperize POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libllama.so.0.0.0 $<TARGET_FILE_DIR:Paperize>/libllama.so.0
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libllama.so.0.0.0 $<TARGET_FILE_DIR:Paperize>/libllama.so
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml.so.0.15.2 $<TARGET_FILE_DIR:Paperize>/libggml.so.0
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml.so.0.15.2 $<TARGET_FILE_DIR:Paperize>/libggml.so
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml-base.so.0.15.2 $<TARGET_FILE_DIR:Paperize>/libggml-base.so.0
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml-base.so.0.15.2 $<TARGET_FILE_DIR:Paperize>/libggml-base.so
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml-cpu.so.0.15.2 $<TARGET_FILE_DIR:Paperize>/libggml-cpu.so.0
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libggml-cpu.so.0.15.2 $<TARGET_FILE_DIR:Paperize>/libggml-cpu.so
    COMMENT "Creating llama.cpp .so symlinks"
)

# Backend-specific symlinks
if(LLAMA_VARIANT STREQUAL "cuda")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libggml-cuda.so.0.15.2 $<TARGET_FILE_DIR:Paperize>/libggml-cuda.so.0
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libggml-cuda.so.0.15.2 $<TARGET_FILE_DIR:Paperize>/libggml-cuda.so
        COMMENT "Creating libggml-cuda symlinks"
    )
elseif(LLAMA_VARIANT STREQUAL "vulkan")
    add_custom_command(TARGET Paperize POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libggml-vulkan.so.0.15.2 $<TARGET_FILE_DIR:Paperize>/libggml-vulkan.so.0
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            libggml-vulkan.so.0.15.2 $<TARGET_FILE_DIR:Paperize>/libggml-vulkan.so
        COMMENT "Creating libggml-vulkan symlinks"
    )
endif()

# Fix rpath of all real .so so they find each other via $ORIGIN
# Written at configure time to a temp script to avoid cmake expanding $ORIGIN
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fix_llama_rpath.sh"
    "#!/bin/bash\n"
    "DIR=\"$1\"\n"
    "for f in \"$DIR\"/libllama.so.0.0.0 \"$DIR\"/libggml.so.0.15.2"
    " \"$DIR\"/libggml-base.so.0.15.2 \"$DIR\"/libggml-cpu.so.0.15.2"
)
if(LLAMA_VARIANT STREQUAL "cuda")
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/fix_llama_rpath.sh"
        " \"$DIR\"/libggml-cuda.so.0.15.2")
elseif(LLAMA_VARIANT STREQUAL "vulkan")
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/fix_llama_rpath.sh"
        " \"$DIR\"/libggml-vulkan.so.0.15.2")
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
