cmake_minimum_required(VERSION 3.25)

project(Hodeia VERSION 0.99 LANGUAGES CXX C)

# Release version = numeric project(VERSION) + optional pre-release suffix
# ("rc", "beta"...; empty for a final release).  project(VERSION) only accepts
# digits, and the Windows installer needs the numeric part on its own.
# Shown in the app (About), the installer name and the Arch pkgver/git tag.
set(HODEIA_VERSION_SUFFIX "rc1")
set(HODEIA_VERSION_STRING "${PROJECT_VERSION}${HODEIA_VERSION_SUFFIX}")

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
  # macOS is EXPERIMENTAL — the app is not yet available for macOS, and this
  # build path (which mirrors the Linux schema) is untested.  See
  # cmake/PlatformMac.cmake for the warning.
  include(cmake/PlatformMac.cmake)
else()
  message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# ---------------------------------------------------------------------------
# 4. Qt6 — project setup
#    On Windows this resolves the MSVC build of Qt from the Qt installer
#    (point CMAKE_PREFIX_PATH at it, e.g. C:/Qt/6.x.x/msvc2022_64).
#    Native MSVC + vcpkg — see cmake/PlatformWindows.cmake.
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Concurrent Network Quick QuickControls2 QuickEffects LinguistTools)
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
# 6b. Unit tests (QtTest + CTest) — on by default in Debug builds only, so the
#     packaging builds (Release) are unchanged.  Run: ctest --test-dir <build>
# ---------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_HODEIA_TESTS_DEFAULT ON)
else()
  set(_HODEIA_TESTS_DEFAULT OFF)
endif()
option(HODEIA_BUILD_TESTS "Build the unit tests" ${_HODEIA_TESTS_DEFAULT})
if(HODEIA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# 7. Install
#    Linux: full install tree (/opt/hodeia) + DEB/RPM packages via CPack.
# ---------------------------------------------------------------------------
if(HODEIA_PLATFORM_LINUX)
  include(cmake/PackagingLinux.cmake)
else()
  install(TARGETS Hodeia
      BUNDLE DESTINATION .
      LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()
