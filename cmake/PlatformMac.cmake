# ---------------------------------------------------------------------------
# PlatformMac.cmake
# macOS (Apple Silicon) build rules: dylib shared-lib mapping, Metal backend
# (the only GPU backend on macOS), and the POST_BUILD deployment
# (@executable_path install names, dylib copy, vec0.dylib, libomp, macdeployqt).
#
# Included by the top-level CMakeLists.txt AFTER HODEIA_PLATFORM_MAC=ON.
# ---------------------------------------------------------------------------

set(HODEIA_PLATFORM_MAC ON)

# ===========================================================================
# A. Shared-library name mapping + library paths
# ===========================================================================
set(_PLAT              "mac")
set(_SHARED_LLAMA      "libllama.dylib")
set(_SHARED_LLAMA_COMM "libllama-common.dylib")
set(_SHARED_MTMD       "libmtmd.dylib")
set(_SHARED_GGML       "libggml.dylib")
set(_SHARED_GGML_BASE  "libggml-base.dylib")
set(_SHARED_GGML_CPU   "libggml-cpu.dylib")
set(_SHARED_GGML_METAL "libggml-metal.dylib")
set(_SHARED_GGML_CUDA  "")
set(_SHARED_GGML_VK    "")
set(_VECTOREC_EXT      "vec0.dylib")
set(_STATIC_EXT        ".a")

set(_MUPDF_LIB         "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf${_STATIC_EXT}")
set(_MUPDF_THIRD_LIB   "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf-third${_STATIC_EXT}")
set(_MUPDF_THREADS_LIB "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libmupdf-threads${_STATIC_EXT}")
set(_TESS_LIB          "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libtesseract${_STATIC_EXT}")
set(_LEPTO_LIB         "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/libleptonica${_STATIC_EXT}")
set(_LLAMA_BASE        "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/llamacpp")
set(_SQLITEVEC_LIB     "${CMAKE_SOURCE_DIR}/external/lib/${_PLAT}/sqlitevec/${_VECTOREC_EXT}")

# ===========================================================================
# B. Backend — Metal only
# ===========================================================================
set(CUDA_FOUND OFF)
set(VULKAN_FOUND OFF)
set(LLAMA_BACKEND "metal")
message(STATUS "Backend (macOS): metal")

find_library(METAL_FRAMEWORK Metal)
if(METAL_FRAMEWORK)
  message(STATUS "Metal framework found")
endif()

string(TOUPPER "${LLAMA_BACKEND}" _upper)
add_compile_definitions("HODEIA_BACKEND_${_upper}")

set(_LLAMA_LINK_DIR "${_LLAMA_BASE}/metal")

# ===========================================================================
# C. Platform-specific system libraries
# ===========================================================================
# MuPDF is built with MUPDF_BUILD_THIRD_PARTY=ON (embeds png/jpeg/tiff/zlib/
# freetype/harfbuzz/openjpeg), so only the Metal framework + m are linked.
set(HODEIA_SYSTEM_LIBS ${METAL_FRAMEWORK} m)

# OpenMP (ggml-cpu) — the shared runtime must be shipped alongside the app.
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
  message(STATUS "OpenMP found")
  get_target_property(_OMP_LIB OpenMP::OpenMP_C LOCATION)
  get_filename_component(_OMP_DIR "${_OMP_LIB}" DIRECTORY)
  # libomp.dylib sits beside libomp.a inside the LLVM prefix/lib/
  set(_OMP_DYLIB "${_OMP_DIR}/libomp.dylib")
else()
  message(STATUS "OpenMP not found — ggml-cpu will run single-threaded")
endif()

# ===========================================================================
# D. llama.cpp backend-specific shared libs
# ===========================================================================
set(_BACKEND_SHARED_LIBS "")
if(_SHARED_GGML_METAL)
  list(APPEND _BACKEND_SHARED_LIBS "${_LLAMA_BASE}/metal/${_SHARED_GGML_METAL}")
endif()

# --- Link variables consumed by src/CMakeLists.txt ---
set(MUPDF_LIB_TARGETS
    ${_MUPDF_LIB}
    ${_MUPDF_THIRD_LIB}
    ${_MUPDF_THREADS_LIB}
    ${_TESS_LIB}
    ${_LEPTO_LIB})

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
# E. POST_BUILD — deployment (requires the Hodeia target to exist)
#    Called from src/CMakeLists.txt after the target is created.
# ===========================================================================
function(hodeia_mac_deploy TARGET)
  # The executable lives in <app>.app/Contents/MacOS/; assets go next to it.

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

  # --- Base .dylib ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA}
          ${_LLAMA_LINK_DIR}/${_SHARED_LLAMA_COMM}
          ${_LLAMA_LINK_DIR}/${_SHARED_MTMD}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_BASE}
          ${_LLAMA_LINK_DIR}/${_SHARED_GGML_CPU}
          $<TARGET_FILE_DIR:${TARGET}>
      COMMENT "Copying llama.cpp base .dylib")

  # --- Backend .dylib (Metal) ---
  if(_BACKEND_SHARED_LIBS)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_BACKEND_SHARED_LIBS}
            $<TARGET_FILE_DIR:${TARGET}>
        COMMENT "Copying llama.cpp backend .dylib (${LLAMA_BACKEND})")
  endif()

  # --- Fix @rpath → @executable_path on all base .dylib ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND install_name_tool -id @executable_path/${_SHARED_LLAMA}      $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_LLAMA}
      COMMAND install_name_tool -id @executable_path/${_SHARED_LLAMA_COMM} $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_LLAMA_COMM}
      COMMAND install_name_tool -id @executable_path/${_SHARED_MTMD}       $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_MTMD}
      COMMAND install_name_tool -id @executable_path/${_SHARED_GGML}       $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_GGML}
      COMMAND install_name_tool -id @executable_path/${_SHARED_GGML_BASE}  $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_GGML_BASE}
      COMMAND install_name_tool -id @executable_path/${_SHARED_GGML_CPU}   $<TARGET_FILE_DIR:${TARGET}>/${_SHARED_GGML_CPU}
      COMMENT "Setting @executable_path install names on .dylib")

  if(_BACKEND_SHARED_LIBS)
    set(_MAC_INSTALL_NAME_CMDS "")
    foreach(_BE_LIB IN LISTS _BACKEND_SHARED_LIBS)
      get_filename_component(_BE_NAME "${_BE_LIB}" NAME)
      list(APPEND _MAC_INSTALL_NAME_CMDS
        COMMAND install_name_tool -id @executable_path/${_BE_NAME} $<TARGET_FILE_DIR:${TARGET}>/${_BE_NAME})
    endforeach()
    add_custom_command(TARGET ${TARGET} POST_BUILD
        ${_MAC_INSTALL_NAME_CMDS}
        COMMENT "Setting @executable_path on backend .dylib")
  endif()

  # --- sqlite-vec ---
  add_custom_command(TARGET ${TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${_SQLITEVEC_LIB}
          $<TARGET_FILE_DIR:${TARGET}>/vec0.dylib
      COMMENT "Copying sqlite-vec extension")

  # --- OpenMP runtime ---
  if(OpenMP_CXX_FOUND AND EXISTS "${_OMP_DYLIB}")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_OMP_DYLIB}" $<TARGET_FILE_DIR:${TARGET}>/libomp.dylib
        COMMAND install_name_tool -id @executable_path/libomp.dylib
            $<TARGET_FILE_DIR:${TARGET}>/libomp.dylib
        COMMENT "Copying and fixing libomp.dylib")
  endif()

  # =========================================================================
  # macdeployqt — ships all Qt frameworks, plugins, and resources into the
  # .app bundle so the user gets a standalone, drag-and-drop application.
  # =========================================================================
  find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
    HINTS "${CMAKE_PREFIX_PATH}" "${Qt6_DIR}/../../../bin"
    PATH_SUFFIXES "bin")

  if(MACDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${MACDEPLOYQT_EXECUTABLE}
            $<TARGET_BUNDLE_DIR:${TARGET}>
            -always-overwrite
        COMMENT "Running macdeployqt (shipping Qt frameworks into .app)")
  else()
    message(WARNING "macdeployqt not found — Qt frameworks will NOT be bundled.\n"
                    "Set CMAKE_PREFIX_PATH to your Qt installation prefix,\n"
                    "or manually run macdeployqt on the built .app.")
  endif()
endfunction()
