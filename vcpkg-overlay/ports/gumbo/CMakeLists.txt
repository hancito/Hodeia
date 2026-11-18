# Build of MuPDF's gumbo fork, exported like the stock vcpkg port
# (find_package(unofficial-gumbo CONFIG) -> unofficial::gumbo::gumbo).
cmake_minimum_required(VERSION 3.16.0)

project(gumbo LANGUAGES C)

file(GLOB gumbo_srcs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(gumbo ${gumbo_srcs})

set_target_properties(gumbo PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    PUBLIC_HEADER "src/gumbo.h;src/tag_enum.h"
)

# MSVC has no <strings.h>: the fork ships one in visualc/include.
if(MSVC)
  set(VISUALC_INCLUDE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/visualc/include>")
else()
  set(VISUALC_INCLUDE "")
endif()

target_include_directories(gumbo PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    ${VISUALC_INCLUDE}
    $<INSTALL_INTERFACE:include/>
)

install(
    TARGETS gumbo EXPORT unofficial-gumbo-config
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)

install(EXPORT unofficial-gumbo-config
    NAMESPACE unofficial::gumbo::
    DESTINATION share/unofficial-gumbo
)
