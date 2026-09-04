# ---------------------------------------------------------------------------
# src/CMakeLists.txt
# Target definitions: PaperizeEngine (static lib for all C++ business logic)
# and Paperize (the Qt6 Quick executable + QML module).
#
# Platform-specific variables (library paths, system libs, backend shared
# libs, include dirs) are set by the active cmake/Platform<OS>.cmake file
# before this file is processed.
# ---------------------------------------------------------------------------

# ===========================================================================
# PaperizeEngine — static library for all C++ business logic
# ===========================================================================
add_library(PaperizeEngine STATIC
    # --- headers (public) ---
    ../include/thememanager.h
    ../include/configmanager.h
    ../include/database.h
    ../include/summary.h
    ../include/query.h
    ../include/summaryproxymodel.h
    ../include/queryproxymodel.h
    ../include/ingestionengine.h
    ../include/enginemanager.h
    ../include/sqlitedb.h
    ../include/sqlitevec.h
    ../include/localbackend.h
    ../include/inferenceengine.h
    ../include/remotebackend.h
    ../include/summarizer.h
    ../include/summarythreadmanager.h
    ../include/queryworker.h
    ../include/querycontroller.h
    ../include/helpers.h

    # --- sources ---
    thememanager.cpp
    configmanager.cpp
    database.cpp
    summary.cpp
    query.cpp
    summaryproxymodel.cpp
    queryproxymodel.cpp
    ingestionengine.cpp
    enginemanager.cpp
    sqlitedb.cpp
    sqlitevec.cpp
    localbackend.cpp
    inferenceengine.cpp
    remotebackend.cpp
    summarizer.cpp
    summarythreadmanager.cpp
    queryworker.cpp
    querycontroller.cpp
    helpers.cpp

    # --- third-party C source (SQLite amalgamation, kept as C, not C++) ---
    ${EXTERNAL}/sqlite3/sqlite3.c
)
set_source_files_properties(${EXTERNAL}/sqlite3/sqlite3.c PROPERTIES LANGUAGE C)

# --- Include directories ---
#   ${CMAKE_SOURCE_DIR}         → also gives direct access to include/ (flat).
#   ${CMAKE_SOURCE_DIR}/include → PUBLIC staging area, isolated from external/.
#   external include dirs       → third-party (llama.cpp, MuPDF, tesseract, sqlite3).
target_include_directories(PaperizeEngine
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${LLAMA_INC}
        ${LLAMA_INC}/vendor
        ${EXTERNAL}/include
        ${MUPDF_INC}
        ${SQLITEVEC_INC}
        ${TESS_INC}
        ${EXTERNAL}/sqlite3
)

# Windows: MSYS2-provided header dirs (mupdf/fitz.h, tesseract/baseapi.h).
if(PAPERIZE_PLATFORM_WIN AND PAPERIZE_MSYS_INCLUDES)
  target_include_directories(PaperizeEngine PRIVATE ${PAPERIZE_MSYS_INCLUDES})
endif()

# --- Link: Qt core modules + third-party libs + system libs + llama.cpp ---
#   * PaperizeEngine links Qt6::Concurrent (used across the workers).
#   * Paperize (the exe) links Qt6::Gui via Qt6::Quick's Qt6::Gui dependency,
#     so PaperizeEngine does not need to link it directly.
target_link_libraries(PaperizeEngine
    PRIVATE
        Qt6::Concurrent
        Qt6::Gui
        Qt6::Network
        ${MUPDF_LIB_TARGETS}
        ${PAPERIZE_SYSTEM_LIBS}
        ${LLAMA_LINK_LIBS}
)

# OpenMP (ggml-cpu parallelism)
if(OpenMP_CXX_FOUND)
  target_link_libraries(PaperizeEngine PRIVATE OpenMP::OpenMP_C)
endif()

# LTO (opt-in)
if(PAPERIZE_ENABLE_LTO)
  set_target_properties(PaperizeEngine PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# ===========================================================================
# Paperize — main executable + QML module
# ===========================================================================
qt_add_executable(Paperize main.cpp)

set_source_files_properties(../ui/Colors.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(../ui/Fonts.qml  PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# QML files live in project/ui/ but the CMakeLists is in project/src/.
# qt_add_qml_module resolves QML_FILES relative to the current source dir,
# so "../ui/Main.qml" would create a "ui/" subfolder in the module output.
# Using QT_RESOURCE_ALIAS maps each file to its bare filename at the module root.
set(_QML_FILES
    Colors.qml Fonts.qml Main.qml MyMenuBar.qml DisplayPane.qml
    SummaryDbPane.qml RagWindow.qml RagDbPane.qml QueryPane.qml
    BackendWindow.qml MyToolBar.qml)
foreach(_f ${_QML_FILES})
  set_source_files_properties(${CMAKE_SOURCE_DIR}/ui/${_f}
      PROPERTIES QT_RESOURCE_ALIAS "${_f}")
endforeach()

qt_add_qml_module(Paperize
    URI "PaperizeCPP"
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
)

# Resources
set(_RES_FILES
    ai-svgrepo-com.svg align-justify.svg arrow-right-to-file.svg edit.svg
    file-export.svg folder-open.svg gear.svg list.svg paperize.ico paperize.png
    refresh.svg save.svg share-square.svg trash-can.svg circle-check.svg
    circle-xmark.svg arrow-circle-left.svg arrow-circle-right.svg
    magnifying-glass-plus.svg magnifying-glass-minus.svg arrow-rotate-back.svg)
foreach(_f ${_RES_FILES})
  set_source_files_properties(${CMAKE_SOURCE_DIR}/Resources/${_f}
      PROPERTIES QT_RESOURCE_ALIAS "Resources/${_f}")
endforeach()

qt_add_resources(Paperize "resources"
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
        ${CMAKE_SOURCE_DIR}/Resources/paperize.ico
        ${CMAKE_SOURCE_DIR}/Resources/paperize.png
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
)

# Link PaperizeEngine + Qt GUI components
target_link_libraries(Paperize
    PRIVATE
        PaperizeEngine
        Qt6::Quick
        Qt6::QuickControls2
)

target_include_directories(Paperize
    PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${LLAMA_INC}
        ${EXTERNAL}/include
)

# ===========================================================================
# Target properties
# ===========================================================================
set_target_properties(Paperize PROPERTIES
    MACOSX_BUNDLE                 TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION  ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    WIN32_EXECUTABLE              TRUE
    BUILD_RPATH                   "$ORIGIN"
    INSTALL_RPATH                 "$ORIGIN"
)

# On macOS, point the install_name at the executable dir.
if(PAPERIZE_PLATFORM_MAC)
  set_target_properties(Paperize PROPERTIES
    BUILD_RPATH   "@executable_path"
    INSTALL_RPATH "@executable_path"
  )
endif()

# LTO (opt-in)
if(PAPERIZE_ENABLE_LTO)
  set_target_properties(Paperize PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
endif()

# ===========================================================================
# POST_BUILD deployment — delegates to the active platform
# ===========================================================================
if(PAPERIZE_PLATFORM_LINUX)
  paperize_linux_deploy(Paperize)
elseif(PAPERIZE_PLATFORM_MAC)
  paperize_mac_deploy(Paperize)
elseif(PAPERIZE_PLATFORM_WIN)
  paperize_windows_deploy(Paperize)
endif()
