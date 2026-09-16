cmake_minimum_required(VERSION 3.25)

project(Hodeia VERSION 0.1 LANGUAGES CXX C)

# ---------------------------------------------------------------------------
# 1. Parallel build — use all cores by default (override: -DCMAKE_BUILD_PARALLEL_LEVEL=4)
# ---------------------------------------------------------------------------
if(NOT DEFINED CMAKE_BUILD_PARALLEL_LEVEL)
  include(ProcessorCount)
  ProcessorCount(NPROC)
  if(NPROC GREATER 0)
    set(CMAKE_BUILD_PARALLEL_LEVEL ${NPROC} CACHE STRING "Build parallelism" FORCE)
    message(STATUS "Build parallelism: ${NPROC} cores")
  endif()
endif()

# ---------------------------------------------------------------------------
# 2. Compiler flags (warnings, optimisation, LTO, C++23)
# ---------------------------------------------------------------------------
include(cmake/CompilerFlags.cmake)

# ---------------------------------------------------------------------------
# 3. Platform detection + per-platform routing
#    Each cmake/Platform<OS>.cmake file sets its own link variables
#    (_MUPDF_LIB*, _TESS_LIB, _LEPTO_LIB, _SQLITEVEC_LIB, MUPDF_LIB_TARGETS,
#     HODEIA_SYSTEM_LIBS, LLAMA_LINK_LIBS, _LLAMA_LINK_DIR, etc.).
# ---------------------------------------------------------------------------
set(HODEIA_PLATFORM_LINUX OFF)
set(HODEIA_PLATFORM_WIN   OFF)
set(HODEIA_PLATFORM_MAC   OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include(cmake/PlatformLinux.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  include(cmake/PlatformWindows.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  include(cmake/PlatformMac.cmake)
else()
  message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# ---------------------------------------------------------------------------
# 4. Qt6 — project setup
#    (On Windows with HODEIA_QT_FROM_MSYS2=ON, the MSYS2 prefix has already
#     been prepended to CMAKE_PREFIX_PATH by cmake/PlatformWindows.cmake.)
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Concurrent Network Quick QuickControls2 LinguistTools)
qt_standard_project_setup(REQUIRES 6.10)

# ---------------------------------------------------------------------------
# 5. Common include directories (platform-independent third-party includes)
# ---------------------------------------------------------------------------
set(EXTERNAL      ${CMAKE_SOURCE_DIR}/external)
set(LLAMA_INC     ${EXTERNAL}/include/llamacpp)
set(SQLITEVEC_INC ${EXTERNAL}/include/SqliteVec)
set(MUPDF_INC     ${EXTERNAL}/include/mupdf)
set(TESS_INC      ${EXTERNAL}/include/tesseract)

# ---------------------------------------------------------------------------
# 6. Core targets (HodeiaEngine static lib + Hodeia executable)
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# 7. Install
# ---------------------------------------------------------------------------
install(TARGETS Hodeia
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
