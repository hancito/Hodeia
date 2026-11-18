# Overlay port: build gumbo from the copy bundled with MuPDF.
#
# The stock vcpkg port downloads gumbo from codeberg.org, which has been
# unreachable from the CI runners (HTTP 504, then DNS failures).  MuPDF ships
# its own fork as the thirdparty/gumbo-parser submodule, fetched from GitHub
# with the MuPDF checkout: build that one, the gumbo MuPDF is tested with.
#
# Same source location as the libmupdf overlay port (MUPDF_LOCAL_SOURCE).
# update-mupdf-stamp.ps1 writes the submodule commit to source-stamp.txt in
# this directory, so a new gumbo commit changes the package ABI.

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

if(DEFINED ENV{MUPDF_LOCAL_SOURCE})
  set(MUPDF_LOCAL_SOURCE "$ENV{MUPDF_LOCAL_SOURCE}")
elseif(NOT DEFINED MUPDF_LOCAL_SOURCE)
  set(MUPDF_LOCAL_SOURCE "D:/Developpement/Dependencies/mupdf")
endif()
get_filename_component(GUMBO_SOURCE
    "${MUPDF_LOCAL_SOURCE}/thirdparty/gumbo-parser" ABSOLUTE)

if(NOT EXISTS "${GUMBO_SOURCE}/src/gumbo.h")
  message(FATAL_ERROR
        "gumbo sources not found in ${GUMBO_SOURCE}.\n"
        "Fetch the MuPDF submodules: git -C <mupdf> submodule update --init")
endif()

# Build from a copy: never write into the MuPDF checkout.
set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/gumbo-parser")
file(REMOVE_RECURSE "${SOURCE_PATH}")
file(COPY "${GUMBO_SOURCE}/src" "${GUMBO_SOURCE}/visualc" "${GUMBO_SOURCE}/COPYING"
     DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME unofficial-gumbo CONFIG_PATH share/unofficial-gumbo)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
