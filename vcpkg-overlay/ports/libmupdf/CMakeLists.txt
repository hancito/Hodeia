cmake_minimum_required(VERSION 3.10)
project(libmupdf)

set(BIN2COFF_EXECUTABLE "bin2coff" CACHE STRING "Host executable for font conversion (Windows only)")

# dependencies
find_package(freetype CONFIG REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(JPEG REQUIRED)
find_package(OpenJPEG CONFIG REQUIRED)
find_package(unofficial-gumbo CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

find_path(JBIG2DEC_INCLUDE_DIR NAMES jbig2.h REQUIRED)
find_library(JBIG2DEC_LIBRARY NAMES jbig2decd jbig2dec NAMES_PER_DIR REQUIRED)

# font resources
if (CMAKE_HOST_WIN32 AND WIN32)
  if(BIN2COFF_EXECUTABLE STREQUAL "bin2coff")
    add_executable(bin2coff EXCLUDE_FROM_ALL scripts/bin2coff.c)
    install(TARGETS bin2coff DESTINATION manual-tools/libmupdf)
  endif()
  if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86")
    set(coff_arch "Win32")
  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "ARM64")
    set(coff_arch "aarch64")
  else()
    set(coff_arch "x64")
  endif()
endif()

file(GLOB fonts
  "${PROJECT_SOURCE_DIR}/resources/fonts/urw/*.cff"
  "${PROJECT_SOURCE_DIR}/resources/fonts/han/*.ttc"
  "${PROJECT_SOURCE_DIR}/resources/fonts/droid/DroidSansFallbackFull.ttf"
  "${PROJECT_SOURCE_DIR}/resources/fonts/droid/DroidSansFallback.ttf"
  "${PROJECT_SOURCE_DIR}/resources/fonts/noto/*.otf"
  "${PROJECT_SOURCE_DIR}/resources/fonts/noto/*.ttf"
  "${PROJECT_SOURCE_DIR}/resources/fonts/sil/*.cff"
)
set(FONT_SOURCES "")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/resources")
foreach(font IN LISTS fonts)
  cmake_path(GET font FILENAME name)
  if (CMAKE_HOST_WIN32 AND WIN32)
    string(REGEX REPLACE "[-.]" "_" identifier "_binary_${name}")
    set(font_object "${CMAKE_CURRENT_BINARY_DIR}/resources/${name}.obj")
    set_source_files_properties("${font_object}" PROPERTIES EXTERNAL_OBJECT 1)
    add_custom_command(OUTPUT "${font_object}"
      COMMAND "${BIN2COFF_EXECUTABLE}" "${font}" "${font_object}" "${identifier}" ${coff_arch}
      VERBATIM
    )
    list(APPEND FONT_SOURCES "${font_object}")
  else()
    set(font_c_source "${CMAKE_CURRENT_BINARY_DIR}/resources/${name}.c")
    add_custom_command(OUTPUT "${font_c_source}"
      COMMAND bash -c "bash ${PROJECT_SOURCE_DIR}/scripts/hexdump.sh '${font}' > '${font_c_source}'"
      VERBATIM
    )
    list(APPEND FONT_SOURCES "${font_c_source}")
  endif()
endforeach()

# Hyphenation dictionary (embedded binary, like fonts)
set(HYPH_ZIP "${PROJECT_SOURCE_DIR}/resources/hyphen/hyph-all.zip")
if (CMAKE_HOST_WIN32 AND WIN32 AND EXISTS "${HYPH_ZIP}")
  set(hyph_object "${CMAKE_CURRENT_BINARY_DIR}/resources/hyph-all.zip.obj")
  set_source_files_properties("${hyph_object}" PROPERTIES EXTERNAL_OBJECT 1)
  add_custom_command(OUTPUT "${hyph_object}"
    COMMAND "${BIN2COFF_EXECUTABLE}" "${HYPH_ZIP}" "${hyph_object}" "_binary_hyph_all_zip" ${coff_arch}
    VERBATIM
  )
  list(APPEND FONT_SOURCES "${hyph_object}")
endif()

# libmupdf
file(GLOB_RECURSE SOURCES
  "source/fitz/*.c"
  "source/pdf/*.c"
  "source/xps/*.c"
  "source/svg/*.c"
  "source/html/*.c"
  "source/reflow/*.c"
  "source/cbz/*.c"
)

# cmark-gfm (vendored in MuPDF source tree)
file(GLOB CMARK_SOURCES "thirdparty/cmark-gfm/src/*.c")
list(FILTER CMARK_SOURCES EXCLUDE REGEX "main\\.c$")
list(FILTER CMARK_SOURCES EXCLUDE REGEX "commonmark\\.c$")
list(FILTER CMARK_SOURCES EXCLUDE REGEX "man\\.c$")
# cmark-gfm extensions (tables, strikethrough, tasklist, etc.)
file(GLOB CMARK_EXT_SOURCES "thirdparty/cmark-gfm/extensions/*.c")
list(APPEND SOURCES ${CMARK_SOURCES} ${CMARK_EXT_SOURCES})

# cmark-gfm generated headers absent from the tree; stub them out
set(CMARK_EXPORT_H "${CMAKE_CURRENT_BINARY_DIR}/cmark-gfm_export.h")
file(WRITE "${CMARK_EXPORT_H}"
"#ifndef CMARK_GFM_EXPORT_H\n#define CMARK_GFM_EXPORT_H\n#define CMARK_GFM_EXPORT\n#define CMARK_GFM_NO_EXPORT\n#endif\n")
set(CMARK_VERSION_H "${CMAKE_CURRENT_BINARY_DIR}/cmark-gfm_version.h")
file(WRITE "${CMARK_VERSION_H}"
"#ifndef CMARK_GFM_VERSION_H\n#define CMARK_GFM_VERSION_H\n#define CMARK_VERSION_MAJOR 0\n#define CMARK_VERSION_MINOR 29\n#define CMARK_VERSION_RELEASE 0\n#define CMARK_VERSION_STRING \"0.29.0\"\n#define CMARK_GFM_VERSION 0x001D0000\n#define CMARK_GFM_VERSION_STRING \"0.29.0\"\n#endif\n")
set(CMARK_CONFIG_H "${CMAKE_CURRENT_BINARY_DIR}/config.h")
file(WRITE "${CMARK_CONFIG_H}"
"#ifndef CMARK_CONFIG_H\n#define CMARK_CONFIG_H\n#define HAVE_STDBOOL_H\n#include <stdbool.h>\n#define CMARK_ATTRIBUTE(list)\n#define CMARK_INLINE __inline\n#endif\n")

add_library(libmupdf ${SOURCES} ${FONT_SOURCES})
set_target_properties(libmupdf PROPERTIES PREFIX "")
target_include_directories(libmupdf
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${PROJECT_SOURCE_DIR}/thirdparty/cmark-gfm/src
    ${PROJECT_SOURCE_DIR}/thirdparty/cmark-gfm/extensions
    ${PROJECT_SOURCE_DIR}/thirdparty/cmark-gfm/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${JBIG2DEC_INCLUDE_DIR}
)
target_compile_definitions(libmupdf
  PRIVATE
    FZ_ENABLE_BROTLI=0
    FZ_ENABLE_DOCX_OUTPUT=0
    FZ_ENABLE_ICC=0
    FZ_ENABLE_JS=0
    FZ_ENABLE_ODT_OUTPUT=0
    MEMENTO_MUPDF_HACKS
    SHARE_JPEG
    OCR_DISABLED
    CMARK_STATIC
)
target_link_libraries(libmupdf
  PRIVATE
    freetype
    harfbuzz::harfbuzz
    ${JBIG2DEC_LIBRARY}
    JPEG::JPEG
    openjp2
    unofficial::gumbo::gumbo
    ZLIB::ZLIB
    $<$<BOOL:${ANDROID}>:log>
)

# installation
install(DIRECTORY include/mupdf
  DESTINATION include
)
install(TARGETS libmupdf
  EXPORT unofficial-libmupdf-targets
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
install(EXPORT unofficial-libmupdf-targets
  NAMESPACE unofficial::libmupdf::
  DESTINATION share/unofficial-libmupdf
)
configure_file(unofficial-libmupdf-config.cmake.in unofficial-libmupdf-config.cmake @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/unofficial-libmupdf-config.cmake"
  DESTINATION share/unofficial-libmupdf
)
