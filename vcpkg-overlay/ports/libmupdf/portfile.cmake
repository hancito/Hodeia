# Overlay port: build MuPDF 1.28.5 from a local source checkout.
#
# The local source path is read from the MUPDF_LOCAL_SOURCE environment
# variable, which build_windows.ps1 sets before invoking vcpkg install:
#
#   $env:MUPDF_LOCAL_SOURCE = "D:\Developpement\Dependencies\mupdf"
#   vcpkg install --triplet x64-windows --overlay-ports ...

if(DEFINED ENV{MUPDF_LOCAL_SOURCE})
  set(MUPDF_LOCAL_SOURCE "$ENV{MUPDF_LOCAL_SOURCE}")
elseif(NOT DEFINED MUPDF_LOCAL_SOURCE)
  message(FATAL_ERROR
        "MUPDF_LOCAL_SOURCE is not set.\n"
        "Set the environment variable before running vcpkg install, e.g.:\n"
        "  $env:MUPDF_LOCAL_SOURCE = \"D:\\Developpement\\Dependencies\\mupdf\"")
endif()
get_filename_component(SOURCE_PATH "${MUPDF_LOCAL_SOURCE}" ABSOLUTE)

if(NOT EXISTS "${SOURCE_PATH}/Makefile")
  message(FATAL_ERROR
        "MUPDF_LOCAL_SOURCE does not look like a MuPDF checkout: ${SOURCE_PATH}\n"
        "Expected to find a Makefile at that path.")
endif()

if(VCPKG_TARGET_IS_WINDOWS)
  vcpkg_check_linkage(ONLY_STATIC_LIBRARY)
endif()

# Copy the CMake wrapper + config template into the source tree.
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/unofficial-libmupdf-config.cmake.in" DESTINATION "${SOURCE_PATH}")

# The bin2coff host tool is needed to convert font resources to .obj on
# Windows.  It ships in the MuPDF source at scripts/bin2coff.c; the stock
# vcpkg port downloads a pinned revision, but our local checkout already
# has it — verify it exists and use the in-tree copy.
if(NOT EXISTS "${SOURCE_PATH}/scripts/bin2coff.c")
  message(FATAL_ERROR
        "scripts/bin2coff.c not found in ${SOURCE_PATH}.\n"
        "Your MuPDF checkout may be incomplete.")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_SHARED_LIBS=OFF
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME "unofficial-libmupdf")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

set(font_licenses "")
foreach(item IN ITEMS urw/OFL.txt noto/COPYING han/LICENSE.txt droid/NOTICE sil/OFL.txt)
  string(REPLACE "/" " " new_name "# Fonts - ${item}")
  set(file "${CURRENT_BUILDTREES_DIR}/${new_name}")
  file(COPY_FILE "${SOURCE_PATH}/resources/fonts/${item}" "${file}")
  list(APPEND font_licenses "${file}")
endforeach()

vcpkg_install_copyright(
    COMMENT [[
This software includes Base 14 PDF fonts from URW, Noto fonts from Google.
Source Han Serif from Adobe for CJK, DroidSansFallback from Android for CJK,
Charis SIL from SIL.
]]
    FILE_LIST
        "${SOURCE_PATH}/COPYING"
        ${font_licenses}
)
