# ---------------------------------------------------------------------------
# CompilerFlags.cmake
# Central place for language standard, warnings, optimisation and LTO rules.
# Included from the top-level CMakeLists.txt BEFORE targets are created.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD         23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Default build type ------------------------------------------------------
# Only applied when the user did not already pass -DCMAKE_BUILD_TYPE=...
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# --- Options -----------------------------------------------------------------
option(HODEIA_ENABLE_LTO "Enable link-time optimisation (slower builds)" OFF)

# --- Warnings ----------------------------------------------------------------
# GCC / Clang
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
  # Only add -g in Debug; let CMake's CMAKE_BUILD_TYPE handle optimisation
  # (CMAKE_CXX_FLAGS_RELEASE already contains -O3 for GCC/Clang).
  if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "")
    add_compile_options(-g)
  endif()
elseif(MSVC)
  add_compile_options(/W4 /permissive-)
endif()

# --- Sanitizers (GCC / Clang, Debug builds) ---------------------------------
# Comma-separated list passed to -fsanitize=, e.g.:
#   -DHODEIA_SANITIZE=address,undefined   (ASan + UBSan)
#   -DHODEIA_SANITIZE=thread              (TSan — incompatible with ASan)
# Use a dedicated build dir (build/Sanitize) so instrumented objects never mix
# with the regular Debug/Release trees.  Third-party prebuilt libs (llama.cpp,
# MuPDF, Tesseract) are not instrumented; only Hodeia's own code + sqlite3.c.
set(HODEIA_SANITIZE "" CACHE STRING "Sanitizers to enable (e.g. address,undefined or thread)")
if(HODEIA_SANITIZE)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "HODEIA_SANITIZE requires GCC or Clang")
  endif()
  if(HODEIA_SANITIZE MATCHES "thread" AND HODEIA_SANITIZE MATCHES "address")
    message(FATAL_ERROR "HODEIA_SANITIZE: 'thread' cannot be combined with 'address'")
  endif()
  if(HODEIA_ENABLE_LTO)
    message(WARNING "HODEIA_SANITIZE with LTO: stack traces will be less precise")
  endif()
  add_compile_options(-fsanitize=${HODEIA_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=${HODEIA_SANITIZE})
  message(STATUS "Sanitizers: ${HODEIA_SANITIZE}")
endif()

# --- Link-time optimisation --------------------------------------------------
# Applied per-target in src/CMakeLists.txt (INTERFACE so the executable too).
if(HODEIA_ENABLE_LTO)
  if(NOT CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  endif()
  message(STATUS "LTO: enabled")
else()
  message(STATUS "LTO: disabled (enable with -DHODEIA_ENABLE_LTO=ON)")
endif()
