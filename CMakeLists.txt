cmake_minimum_required(VERSION 3.16)

project(Paperize_CPP VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Concurrent Quick QuickControls2 Pdf Sql)

qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(Paperize
    src/main.cpp
)

set_source_files_properties(ui/Colors.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set_source_files_properties(ui/Fonts.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(Paperize
    URI Paperize_CPP
    QML_FILES
        ui/Colors.qml
        ui/Fonts.qml
        ui/Main.qml
        ui/MyMenuBar.qml
        ui/LeftPane.qml
        ui/CentralPane.qml
        ui/RightPane.qml
        ui/RagWindow.qml
        ui/RagDbPane.qml
        ui/QueryPane.qml
        ui/BackendWindow.qml
    SOURCES
        headers/thememanager.h
        headers/configmanager.h
        headers/database.h
        headers/summary.h
        headers/query.h
        headers/ingestionengine.h
        headers/enginemanager.h
        src/thememanager.cpp
        src/configmanager.cpp
        src/database.cpp
        src/summary.cpp
        src/query.cpp
        src/ingestionengine.cpp
        src/enginemanager.cpp
)

# Add resources separately
qt_add_resources(Paperize "resources" PREFIX "/" FILES
    Resources/ai-solid-svgrepo-com.svg
    Resources/ai-svgrepo-com.svg
    Resources/align-justify.svg
    Resources/arrow-right-to-file.svg
    Resources/edit.svg
    Resources/file-export.svg
    Resources/folder-open.svg
    Resources/gear.svg
    Resources/list.svg
    Resources/paperize.ico
    Resources/paperize.png
    Resources/refresh.svg
    Resources/save.svg
    Resources/share-square.svg
    Resources/trash-can.svg
)

# Copy themes folder to build directory
add_custom_command(TARGET Paperize POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/ui/themes
    $<TARGET_FILE_DIR:Paperize>/ui/themes
)

# Copy fonts folder to build directory
add_custom_command(TARGET Paperize POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Fonts
    $<TARGET_FILE_DIR:Paperize>/Fonts
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(Paperize PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appPaperize_CPP
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_include_directories(Paperize PRIVATE
    ${CMAKE_SOURCE_DIR}/external/include
)

target_link_directories(Paperize PRIVATE
    ${CMAKE_SOURCE_DIR}/external/lib
)

target_link_libraries(Paperize PRIVATE
    Qt6::Concurrent
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Pdf
    Qt6::Sql
    mupdf
    mupdf-third
    mupdf-threads
)

install(TARGETS Paperize
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
