# ---------------------------------------------------------------------------
# PackagingLinux.cmake
# install() rules + CPack (DEB / RPM) for Linux.
#
# Layout (identical to the Arch package, package/arch/PKGBUILD):
#   /opt/hodeia/                 self-contained folder — the app resolves every
#                                resource relative to the executable
#     Hodeia, lib*.so* (llama.cpp), vec0.so, Fonts/, i18n/, Resources/Models/,
#     tessdata/, ui/themes/
#     lib/ plugins/ qml/ qt.conf  (only with HODEIA_BUNDLE_QT=ON)
#   /usr/bin/hodeia -> /opt/hodeia/Hodeia
#   /usr/share/applications/hodeia.desktop, icon, doc (LICENSE, NOTICE)
#
# Install destinations are relative to a "/" prefix, so:
#   cmake --install build --prefix /tmp/stage     -> /tmp/stage/opt/hodeia/...
#   cd build && cpack -G "DEB;RPM"                -> hodeia_<ver>_amd64.deb ...
#
# HODEIA_BUNDLE_QT=ON copies Qt (libs, plugins, QML modules) into /opt/hodeia
# with Qt's own deploy API, so one package works on every distro whose glibc is
# at least the build machine's, whatever Qt version it ships.  Use it with a
# Qt installed OUTSIDE the system lib dirs (aqtinstall / Qt online installer):
# with a distro Qt in /usr, the deploy step would copy half of /usr/lib.
#
# Included by the top-level CMakeLists.txt after add_subdirectory(src).
# ---------------------------------------------------------------------------

option(HODEIA_BUNDLE_QT "Bundle Qt into /opt/hodeia (portable DEB/RPM)" OFF)

set(HODEIA_INSTALL_DIR "opt/hodeia")

# --- Executable ---------------------------------------------------------------
# $ORIGIN: llama.cpp .so / vec0.so next to the binary; $ORIGIN/lib: bundled Qt.
if(HODEIA_BUNDLE_QT)
  set_target_properties(Hodeia PROPERTIES INSTALL_RPATH "$ORIGIN;$ORIGIN/lib")
endif()
install(TARGETS Hodeia RUNTIME DESTINATION ${HODEIA_INSTALL_DIR})

# --- llama.cpp shared libs ----------------------------------------------------
# Taken from the build dir, where hodeia_linux_deploy() already copied them and
# patched their RPATH to $ORIGIN.  Globbed at install time: the set of CPU
# variants / GPU backends depends on how llama.cpp was built.  Symlinks
# (libX.so -> libX.so.0) are installed as symlinks.
install(CODE "
  file(GLOB _hodeia_llama_so \"$<TARGET_FILE_DIR:Hodeia>/lib*.so*\")
  file(INSTALL \${_hodeia_llama_so}
       DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${HODEIA_INSTALL_DIR}\"
       USE_SOURCE_PERMISSIONS)
")

# --- sqlite-vec extension -----------------------------------------------------
install(PROGRAMS ${_SQLITEVEC_LIB} DESTINATION ${HODEIA_INSTALL_DIR} RENAME vec0.so)

# --- Resources (same set as hodeia_linux_deploy) ------------------------------
install(DIRECTORY ${CMAKE_SOURCE_DIR}/Fonts           DESTINATION ${HODEIA_INSTALL_DIR})
install(DIRECTORY ${CMAKE_SOURCE_DIR}/ui/themes       DESTINATION ${HODEIA_INSTALL_DIR}/ui)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/Resources/tessdata DESTINATION ${HODEIA_INSTALL_DIR})
install(DIRECTORY ${CMAKE_SOURCE_DIR}/Resources/Models   DESTINATION ${HODEIA_INSTALL_DIR}/Resources)
# Translations (.qm), deployed to <exe>/i18n by Hodeia_deploy_translations.
install(DIRECTORY $<TARGET_FILE_DIR:Hodeia>/i18n DESTINATION ${HODEIA_INSTALL_DIR})

# --- Bundled Qt -----------------------------------------------------------------
if(HODEIA_BUNDLE_QT)
  # Deploy into /opt/hodeia/{lib,plugins,qml} (the layout of a Qt prefix, so
  # the RUNPATHs of Qt's own plugins keep working) + qt.conf next to the exe.
  install(CODE "
    set(QT_DEPLOY_BIN_DIR     \"${HODEIA_INSTALL_DIR}\")
    set(QT_DEPLOY_LIB_DIR     \"${HODEIA_INSTALL_DIR}/lib\")
    set(QT_DEPLOY_PLUGINS_DIR \"${HODEIA_INSTALL_DIR}/plugins\")
    set(QT_DEPLOY_QML_DIR     \"${HODEIA_INSTALL_DIR}/qml\")
  ")
  qt_generate_deploy_qml_app_script(
      TARGET Hodeia
      OUTPUT_SCRIPT _hodeia_qt_deploy_script
      NO_TRANSLATIONS          # the app only loads its own catalog (i18n/)
      # Only the default QPA platform (xcb) is deployed by default: add Wayland.
      INCLUDE_PLUGINS qwayland qwayland-generic qwayland-egl
      # Every plugin pulls its system libraries into the package Depends:
      # leave out the ones a desktop app does not need (embedded / kiosk
      # platforms, input-device backends, QML debugging, the GTK theme which
      # would depend on GTK 3).
      EXCLUDE_PLUGINS qeglfs qlinuxfb qvnc qvkkhrdisplay qgtk3
      EXCLUDE_PLUGIN_TYPES egldeviceintegrations generic qmltooling
      # llama.cpp libs are installed above, next to the binary (ggml dlopens
      # its backends from there); libvulkan must come from the host.
      PRE_EXCLUDE_REGEXES
          "^libllama" "^libmtmd" "^libggml" "^libvulkan"
  )
  install(SCRIPT ${_hodeia_qt_deploy_script})
endif()

# --- Desktop integration --------------------------------------------------------
# Launcher symlink (absolute target: the packages always install to /opt/hodeia).
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/package)
file(CREATE_LINK /${HODEIA_INSTALL_DIR}/Hodeia ${CMAKE_BINARY_DIR}/package/hodeia SYMBOLIC)
install(FILES ${CMAKE_BINARY_DIR}/package/hodeia DESTINATION usr/bin)

# The .desktop file is shared with the Arch package.
install(FILES ${CMAKE_SOURCE_DIR}/package/arch/hodeia.desktop
        DESTINATION usr/share/applications)
install(FILES ${CMAKE_SOURCE_DIR}/Resources/hodeia.png
        DESTINATION usr/share/icons/hicolor/512x512/apps)
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/NOTICE
        DESTINATION usr/share/doc/hodeia)

# ===========================================================================
# CPack — DEB + RPM
# ===========================================================================
# Package version: "0.99~rc1" — the tilde makes a pre-release sort BEFORE the
# final "0.99" in both dpkg and rpm.
if(HODEIA_VERSION_SUFFIX)
  set(_HODEIA_PKG_VERSION "${PROJECT_VERSION}~${HODEIA_VERSION_SUFFIX}")
else()
  set(_HODEIA_PKG_VERSION "${PROJECT_VERSION}")
endif()

set(CPACK_PACKAGE_NAME                "hodeia")
set(CPACK_PACKAGE_VERSION             "${_HODEIA_PKG_VERSION}")
set(CPACK_PACKAGE_VENDOR              "hancito")
set(CPACK_PACKAGE_CONTACT             "garha4@gmail.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Local-AI research assistant for academic papers")
set(CPACK_PACKAGE_DESCRIPTION
    "HodeIA summarizes and queries academic papers (PDF) with local language models (llama.cpp). GPU acceleration through Vulkan when available.")
set(CPACK_PACKAGE_HOMEPAGE_URL        "https://github.com/hancito/Hodeia")
set(CPACK_RESOURCE_FILE_LICENSE       "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX    "/")
set(CPACK_STRIP_FILES                 ON)
set(CPACK_THREADS                     0)    # all cores for compression
if(NOT CPACK_GENERATOR)
  set(CPACK_GENERATOR "DEB;RPM")
endif()

# --- DEB ---
# HODEIA_PKG_DISTRO (e.g. "ubuntu:26.04", set by the CI) is appended to the
# Debian revision: 1~ubuntu26.04 — one file name per target distro, and "~"
# keeps a distro build sorting before a plain "-1".
set(HODEIA_PKG_DISTRO "" CACHE STRING "Target distro tag for the DEB revision (CI)")
set(CPACK_DEBIAN_PACKAGE_RELEASE      1)
if(HODEIA_PKG_DISTRO)
  string(REPLACE ":" "" _HODEIA_DISTRO_TAG "${HODEIA_PKG_DISTRO}")
  set(CPACK_DEBIAN_PACKAGE_RELEASE    "1~${_HODEIA_DISTRO_TAG}")
endif()
set(CPACK_DEBIAN_FILE_NAME            DEB-DEFAULT)   # hodeia_0.99~rc1-1~ubuntu26.04_amd64.deb
set(CPACK_DEBIAN_PACKAGE_SECTION      "science")
# Depends: computed by dpkg-shlibdeps.  The bundled libs (Qt, llama.cpp) are
# resolved through the RUNPATHs ($ORIGIN, $ORIGIN/lib), which CPackDeb passes
# to dpkg-shlibdeps as private search dirs.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS    ON)
set(CPACK_DEBIAN_COMPRESSION_TYPE     "xz")          # readable by every dpkg

# --- RPM ---
set(CPACK_RPM_FILE_NAME               RPM-DEFAULT)   # hodeia-0.99~rc1-1.x86_64.rpm
set(CPACK_RPM_PACKAGE_RELEASE         1)
set(CPACK_RPM_PACKAGE_LICENSE         "AGPL-3.0-only")
set(CPACK_RPM_PACKAGE_GROUP           "Applications/Science")
set(CPACK_RPM_PACKAGE_URL             "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_COMPRESSION_TYPE        "zstd")
# Do not own the system directories (conflicts with the filesystem package).
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /opt
    /usr/share/applications
    /usr/share/icons
    /usr/share/icons/hicolor
    /usr/share/icons/hicolor/512x512
    /usr/share/icons/hicolor/512x512/apps
    /usr/share/doc)
# - Never advertise the bundled libraries (Qt, ICU, llama.cpp) as provided by
#   this package, and do not require them from the system.
# - No debuginfo sub-package / build-id links (stripped, prebuilt payload).
set(CPACK_RPM_SPEC_MORE_DEFINE
"%global __provides_exclude_from ^/opt/hodeia/.*$
%global __requires_exclude ^lib(Qt6|icu|llama|mtmd|ggml).*$
%global debug_package %{nil}
%global _build_id_links none")

include(CPack)
