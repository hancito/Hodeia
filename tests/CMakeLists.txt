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
endfunction()

hodeia_add_test(tst_anthropicapi)
hodeia_add_test(tst_chatbackend)
hodeia_add_test(tst_summaryformat)
hodeia_add_test(tst_metadataresolver)
hodeia_add_test(tst_citationformat)
hodeia_add_test(tst_retriever)
hodeia_add_test(tst_querypromptbuilder)
hodeia_add_test(tst_summarypromptbuilder)
