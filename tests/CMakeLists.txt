# ---------------------------------------------------------------------------
# tests/CMakeLists.txt — one QtTest executable per tested unit, linked
# against HodeiaEngine (the app's business logic, without the QML UI).
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Test)

function(hodeia_add_test name)
  qt_add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE HodeiaEngine Qt6::Test Qt6::Network Qt6::Gui)
  add_test(NAME ${name} COMMAND ${name})
  # No display needed: QtTest's offscreen platform for any Gui code.
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  # SqliteDb::open() loads the sqlite-vec extension from the executable's
  # folder, as for the app.
  if(EXISTS "${_SQLITEVEC_LIB}")
    if(WIN32)
      set(_vec0 vec0.dll)
    else()
      set(_vec0 vec0.so)
    endif()
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_SQLITEVEC_LIB} $<TARGET_FILE_DIR:${name}>/${_vec0})
  endif()
endfunction()

hodeia_add_test(tst_anthropicapi)
hodeia_add_test(tst_chatbackend)
hodeia_add_test(tst_summaryformat)
hodeia_add_test(tst_metadataresolver)
hodeia_add_test(tst_citationformat)
hodeia_add_test(tst_retriever)
hodeia_add_test(tst_querypromptbuilder)
hodeia_add_test(tst_summarypromptbuilder)
hodeia_add_test(tst_projectstore)
hodeia_add_test(tst_libraryrepository)
hodeia_add_test(tst_mcpserver)

# End to end: runs the Hodeia executable itself with --mcp.
hodeia_add_test(tst_mcpstdio)
target_compile_definitions(tst_mcpstdio PRIVATE
    HODEIA_EXE="$<TARGET_FILE:Hodeia>")
add_dependencies(tst_mcpstdio Hodeia)

hodeia_add_test(tst_mcplibrary)
# ggml_backend_load_all() looks next to the executable and in the current
# folder: run from Hodeia's folder, where the ggml backends are deployed.
set_tests_properties(tst_mcplibrary PROPERTIES
    WORKING_DIRECTORY $<TARGET_FILE_DIR:Hodeia>)
add_dependencies(tst_mcplibrary Hodeia)
# The bundled E5 model (Git LFS) for the search test; skipped if absent.
target_compile_definitions(tst_mcplibrary PRIVATE
    HODEIA_E5_MODEL="${CMAKE_SOURCE_DIR}/Resources/Models/Embedders/multilingual-e5-small-q8_0.gguf")
