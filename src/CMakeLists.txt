# ---------------------------------------------------------------------------
# src/CMakeLists.txt
# Target definitions: HodeiaEngine (static lib for all C++ business logic)
# and Hodeia (the Qt6 Quick executable + QML module).
#
# Platform-specific variables (library paths, system libs, backend shared
# libs, include dirs) are set by the active cmake/Platform<OS>.cmake file
# before this file is processed.
# ---------------------------------------------------------------------------

# ===========================================================================
# HodeiaEngine — static library for all C++ business logic
# ===========================================================================
add_library(HodeiaEngine STATIC
    # --- headers (public) ---
    ../include/thememanager.h
    ../include/configmanager.h
    ../include/database.h
    ../include/projectstore.h
    ../include/libraryrepository.h
    ../include/mcpserver.h
    ../include/mcpstdio.h
    ../include/mcplibrary.h
    ../include/apppaths.h
    ../include/zoterosource.h
    ../include/zoterolocator.h
    ../include/zoterosqlitesource.h
    ../include/zoterorepository.h
    ../include/zoterosync.h
    ../include/zoterocollectionmodel.h
    ../include/zoterocontroller.h
    ../include/summary.h
    ../include/query.h
    ../include/summaryproxymodel.h
    ../include/queryproxymodel.h
    ../include/citationsmodel.h
    ../include/ingestionengine.h
    ../include/enginemanager.h
    ../include/sqlitedb.h
    ../include/sqlitevec.h
    ../include/textchunk.h
    ../include/localbackend.h
    ../include/localprompt.h
    ../include/vramfit.h
    ../include/inferenceengine.h
    ../include/remotebackend.h
    ../include/chatbackend.h
    ../include/anthropicapi.h
    ../include/summarizer.h
    ../include/summarythreadmanager.h
    ../include/queryworker.h
    ../include/pagecontext.h
    ../include/retriever.h
    ../include/reindexer.h
    ../include/reindexcontroller.h
    ../include/querypromptbuilder.h
    ../include/summarypromptbuilder.h
    ../include/metadataresolver.h
    ../include/citationformat.h
    ../include/querycontroller.h
    ../include/openalex.h
    ../include/helpers.h
    ../include/notifier.h
    ../include/pdfexport.h
    ../include/summaryformat.h
    ../include/textformat.h

    # --- sources ---
    thememanager.cpp
    configmanager.cpp
    database.cpp
    projectstore.cpp
    libraryrepository.cpp
    mcpserver.cpp
    mcpstdio.cpp
    mcplibrary.cpp
    apppaths.cpp
    zoterolocator.cpp
    zoterosqlitesource.cpp
    zoterorepository.cpp
    zoterosync.cpp
    zoterocollectionmodel.cpp
    zoterocontroller.cpp
    summary.cpp
    query.cpp
    summaryproxymodel.cpp
    queryproxymodel.cpp
    citationsmodel.cpp
    ingestionengine.cpp
    enginemanager.cpp
    sqlitedb.cpp
    sqlitevec.cpp
    localbackend.cpp
    localprompt.cpp
    vramfit.cpp
    inferenceengine.cpp
    remotebackend.cpp
    chatbackend.cpp
    anthropicapi.cpp
    summarizer.cpp
    summarythreadmanager.cpp
    openalex.cpp
    queryworker.cpp
    retriever.cpp
    reindexer.cpp
    reindexcontroller.cpp
    querypromptbuilder.cpp
    summarypromptbuilder.cpp
    metadataresolver.cpp
    citationformat.cpp
    querycontroller.cpp
    helpers.cpp
    notifier.cpp
    pdfexport.cpp
    summaryformat.cpp
    textformat.cpp

    # --- third-party C source (SQLite amalgamation, kept as C, not C++) ---
    ${EXTERNAL}/sqlite3/sqlite3.c
)
set_source_files_properties(${EXTERNAL}/sqlite3/sqlite3.c PROPERTIES LANGUAGE C)

# UBSan: the prebuilt libmupdf.a is not sanitized, so on x86-64 its pool
# allocator aligns structs (fz_stext_block…) to FZ_POINTER_ALIGN_MOD = 4 bytes
# (see fitz/system.h).  Unaligned loads are legal on x86-64, but UBSan reports
# every access from our code.  Disable only the alignment check, only in the
# file that walks MuPDF structures.
if(HODEIA_SANITIZE MATCHES "undefined")
  set_source_files_properties(ingestionengine.cpp PROPERTIES
      COMPILE_OPTIONS "-fno-sanitize=alignment")
endif()

# --- Include directories ---
#   ${CMAKE_SOURCE_DIR}         → also gives direct access to include/ (flat).
#   ${CMAKE_SOURCE_DIR}/include → PUBLIC staging area, isolated from external/.
#   external include dirs       → third-party (llama.cpp, MuPDF, tesseract, sqlite3).
target_include_directories(HodeiaEngine
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        # Public: include/sqlitevec.h includes llama.h (needed by the tests).
        ${LLAMA_INC}
        ${LLAMA_INC}/vendor
    PRIVATE
        ${EXTERNAL}/include
        ${MUPDF_INC}
        ${SQLITEVEC_INC}
        ${TESS_INC}
        ${EXTERNAL}/sqlite3
)

# Note: on Windows (native MSVC) the MuPDF/Tesseract/Leptonica headers come
# from ${EXTERNAL}/include (already above) and the libraries from vcpkg
# (linked via the targets below).  No MSYS2 include dirs are needed.

# --- Link: Qt core modules + third-party libs + system libs + llama.cpp ---
#   * HodeiaEngine links Qt6::Concurrent (used across the workers).
#   * Hodeia (the exe) links Qt6::Gui via Qt6::Quick's Qt6::Gui dependency,
#     so HodeiaEngine does not need to link it directly.
#   * On Windows the OCR path (Tesseract/Leptonica) and the Vulkan backend are
#     vcpkg targets supplied by PlatformWindows.cmake (empty if not found).
target_link_libraries(HodeiaEngine
    PRIVATE
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Network
        ${MUPDF_LIB_TARGETS}
        ${HODEIA_SYSTEM_LIBS}
        ${LLAMA_LINK_LIBS}
)

# OCR (Tesseract/Leptonica): Linux/macOS link the in-tree static libs (set by
# the platform cmake); Windows links the vcpkg targets.  ingestionengine.cpp
# uses the OCR path on all platforms, so this must not be Windows-only.
if(TESS_LEPTO_TARGETS)
  target_link_libraries(HodeiaEngine PRIVATE ${TESS_LEPTO_TARGETS})
endif()

# Windows-only: vcpkg Vulkan target (no-op on Linux/macOS).
if(HODEIA_PLATFORM_WIN AND VULKAN_TARGET)
  target_link_libraries(HodeiaEngine PRIVATE ${VULKAN_TARGET})
endif()

# OpenMP (ggml-cpu parallelism)
if(OpenMP_CXX_FOUND)
  target_link_libraries(HodeiaEngine PRIVATE OpenMP::OpenMP_C)
endif()

# LTO (opt-in)
if(HODEIA_ENABLE_LTO)
  set_target_properties(HodeiaEngine PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# ===========================================================================
# Hodeia — main executable + QML module
# ===========================================================================
qt_add_executable(Hodeia main.cpp)
target_compile_definitions(Hodeia PRIVATE HODEIA_VERSION="${HODEIA_VERSION_STRING}")

# Windows: embed the application icon in Hodeia.exe (used by Explorer, the
# taskbar and every shortcut, including the installer's desktop icon).
if(WIN32)
  target_sources(Hodeia PRIVATE hodeia.rc)
endif()

set_source_files_properties(../ui/Colors.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(../ui/Fonts.qml  PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(../ui/Style.qml  PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# QML files live in project/ui/ but the CMakeLists is in project/src/.
# qt_add_qml_module resolves QML_FILES relative to the current source dir,
# so "../ui/Main.qml" would create a "ui/" subfolder in the module output.
# Using QT_RESOURCE_ALIAS maps each file to its bare filename at the module root.
set(_QML_FILES
    Colors.qml Fonts.qml Main.qml MyMenuBar.qml DisplayPane.qml
    SummaryDbPane.qml RagWindow.qml RagDbPane.qml QueryPane.qml
    BackendWindow.qml MyToolBar.qml ZoteroWindow.qml
    # Shared styled components (see Style.qml)
    Style.qml PopupBackground.qml CheckIndicator.qml StyledButton.qml
    StyledCheckBox.qml StyledComboBox.qml StyledTextField.qml StyledSpinBox.qml
    StyledScrollBar.qml StyledDialog.qml StatusBar.qml SplitHandleBar.qml
    StyledMenu.qml StyledMenuItem.qml StyledMenuSeparator.qml MenuCheckRow.qml
    StyledTabBar.qml StyledTabButton.qml StyledProgressBar.qml StyledGroupBox.qml
    StyledToolTip.qml StyledBusyIndicator.qml ThemedWindow.qml MessageBox.qml
    IngestionDialog.qml)
foreach(_f ${_QML_FILES})
  set_source_files_properties(${CMAKE_SOURCE_DIR}/ui/${_f}
      PROPERTIES QT_RESOURCE_ALIAS "${_f}")
endforeach()

qt_add_qml_module(Hodeia
    URI "Hodeia"
    OUTPUT_DIRECTORY Hodeia_app
    QML_FILES
        ${CMAKE_SOURCE_DIR}/ui/Colors.qml
        ${CMAKE_SOURCE_DIR}/ui/Fonts.qml
        ${CMAKE_SOURCE_DIR}/ui/Main.qml
        ${CMAKE_SOURCE_DIR}/ui/MyMenuBar.qml
        ${CMAKE_SOURCE_DIR}/ui/DisplayPane.qml
        ${CMAKE_SOURCE_DIR}/ui/SummaryDbPane.qml
        ${CMAKE_SOURCE_DIR}/ui/RagWindow.qml
        ${CMAKE_SOURCE_DIR}/ui/RagDbPane.qml
        ${CMAKE_SOURCE_DIR}/ui/QueryPane.qml
        ${CMAKE_SOURCE_DIR}/ui/BackendWindow.qml
        ${CMAKE_SOURCE_DIR}/ui/MyToolBar.qml
        ${CMAKE_SOURCE_DIR}/ui/ZoteroWindow.qml
        ${CMAKE_SOURCE_DIR}/ui/Style.qml
        ${CMAKE_SOURCE_DIR}/ui/PopupBackground.qml
        ${CMAKE_SOURCE_DIR}/ui/CheckIndicator.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledButton.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledCheckBox.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledComboBox.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledTextField.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledSpinBox.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledScrollBar.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledDialog.qml
        ${CMAKE_SOURCE_DIR}/ui/StatusBar.qml
        ${CMAKE_SOURCE_DIR}/ui/SplitHandleBar.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledMenu.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledMenuItem.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledMenuSeparator.qml
        ${CMAKE_SOURCE_DIR}/ui/MenuCheckRow.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledTabBar.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledTabButton.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledProgressBar.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledGroupBox.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledToolTip.qml
        ${CMAKE_SOURCE_DIR}/ui/StyledBusyIndicator.qml
        ${CMAKE_SOURCE_DIR}/ui/ThemedWindow.qml
        ${CMAKE_SOURCE_DIR}/ui/MessageBox.qml
        ${CMAKE_SOURCE_DIR}/ui/IngestionDialog.qml
)

# ---------------------------------------------------------------------------
# Translations
# ---------------------------------------------------------------------------
qt_add_translations(Hodeia
    TS_FILES ${CMAKE_SOURCE_DIR}/translations/hodeia_fr.ts
    QM_FILES_OUTPUT_VARIABLE HODEIA_QM_FILES
)

# Deploy the .qm files to <exe>/i18n/, where main.cpp loads them (all
# platforms).  A custom target, not a POST_BUILD step of Hodeia: POST_BUILD
# only runs when the executable is relinked, so a translation-only change
# left a stale .qm in i18n/.  copy_if_different keeps no-op builds cheap.
if(HODEIA_QM_FILES)
  add_custom_target(Hodeia_deploy_translations ALL
      COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:Hodeia>/i18n
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${HODEIA_QM_FILES} $<TARGET_FILE_DIR:Hodeia>/i18n
      DEPENDS ${HODEIA_QM_FILES}
      COMMENT "Deploying translations (.qm)"
      VERBATIM)
  add_dependencies(Hodeia_deploy_translations Hodeia_lrelease)
endif()

# Resources
set(_RES_FILES
    ai-svgrepo-com.svg align-justify.svg arrow-right-to-file.svg edit.svg
    file-export.svg folder-open.svg gear.svg list.svg hodeia.ico hodeia.png
    refresh.svg save.svg share-square.svg trash-can.svg circle-check.svg
    circle-xmark.svg arrow-circle-left.svg arrow-circle-right.svg
    magnifying-glass-plus.svg magnifying-glass-minus.svg arrow-rotate-back.svg
    file-pdf-solid-full.svg file-pdf-regular-full.svg)
foreach(_f ${_RES_FILES})
  set_source_files_properties(${CMAKE_SOURCE_DIR}/Resources/${_f}
      PROPERTIES QT_RESOURCE_ALIAS "Resources/${_f}")
endforeach()

qt_add_resources(Hodeia "resources"
    PREFIX "/"
    FILES
        ${CMAKE_SOURCE_DIR}/Resources/ai-svgrepo-com.svg
        ${CMAKE_SOURCE_DIR}/Resources/align-justify.svg
        ${CMAKE_SOURCE_DIR}/Resources/arrow-right-to-file.svg
        ${CMAKE_SOURCE_DIR}/Resources/edit.svg
        ${CMAKE_SOURCE_DIR}/Resources/file-export.svg
        ${CMAKE_SOURCE_DIR}/Resources/folder-open.svg
        ${CMAKE_SOURCE_DIR}/Resources/gear.svg
        ${CMAKE_SOURCE_DIR}/Resources/list.svg
        ${CMAKE_SOURCE_DIR}/Resources/hodeia.ico
        ${CMAKE_SOURCE_DIR}/Resources/hodeia.png
        ${CMAKE_SOURCE_DIR}/Resources/refresh.svg
        ${CMAKE_SOURCE_DIR}/Resources/save.svg
        ${CMAKE_SOURCE_DIR}/Resources/share-square.svg
        ${CMAKE_SOURCE_DIR}/Resources/trash-can.svg
        ${CMAKE_SOURCE_DIR}/Resources/circle-check.svg
        ${CMAKE_SOURCE_DIR}/Resources/circle-xmark.svg
        ${CMAKE_SOURCE_DIR}/Resources/arrow-circle-left.svg
        ${CMAKE_SOURCE_DIR}/Resources/arrow-circle-right.svg
        ${CMAKE_SOURCE_DIR}/Resources/magnifying-glass-plus.svg
        ${CMAKE_SOURCE_DIR}/Resources/magnifying-glass-minus.svg
        ${CMAKE_SOURCE_DIR}/Resources/arrow-rotate-back.svg
        ${CMAKE_SOURCE_DIR}/Resources/file-pdf-solid-full.svg
        ${CMAKE_SOURCE_DIR}/Resources/file-pdf-regular-full.svg
)

# Link HodeiaEngine + Qt GUI components
target_link_libraries(Hodeia
    PRIVATE
        HodeiaEngine
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickEffects
)

target_include_directories(Hodeia
    PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${LLAMA_INC}
        ${EXTERNAL}/include
)

# ===========================================================================
# Target properties
# ===========================================================================
set_target_properties(Hodeia PROPERTIES
    MACOSX_BUNDLE                 TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION  ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    WIN32_EXECUTABLE              TRUE
    BUILD_RPATH                   "$ORIGIN"
    INSTALL_RPATH                 "$ORIGIN"
)

# On macOS, point the install_name at the executable dir.
if(HODEIA_PLATFORM_MAC)
  set_target_properties(Hodeia PROPERTIES
    BUILD_RPATH   "@executable_path"
    INSTALL_RPATH "@executable_path"
  )
endif()

# LTO (opt-in)
if(HODEIA_ENABLE_LTO)
  set_target_properties(Hodeia PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# ===========================================================================
# POST_BUILD deployment — delegates to the active platform
# ===========================================================================
if(HODEIA_PLATFORM_LINUX)
  hodeia_linux_deploy(Hodeia)
elseif(HODEIA_PLATFORM_MAC)
  hodeia_mac_deploy(Hodeia)
elseif(HODEIA_PLATFORM_WIN)
  hodeia_windows_deploy(Hodeia)
endif()
