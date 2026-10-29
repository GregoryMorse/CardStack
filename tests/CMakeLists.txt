set(CARDSTACK_WINEVDM_TEST_FIXTURE_DIR
    "${CMAKE_SOURCE_DIR}/tests/fixtures/legacy/winevdm"
)

set(CARDSTACK_TEST_SOURCES
    app/MainWindowActionTests.cpp
    app/MainWindowWorkflowTests.cpp
    app/FocusedAuditRegressionTests.cpp
    security/ImportSecurityTests.cpp
    core/DeckMergeTests.cpp
    core/DelimitedTextTests.cpp
    core/PhoneCallLogTests.cpp
    core/DeckTemplateTests.cpp
    legacy/LegacyDeckReaderTests.cpp
    legacy/LegacyInterchangeReaderTests.cpp
    legacy/LegacyReportReaderTests.cpp
    legacy/btrieve/BtrieveAuditReaderTests.cpp
    storage/SQLiteDeckStoreTests.cpp
    storage/SQLitePackageStoreTests.cpp
    widgets/PhoneticSearchTests.cpp
    widgets/DeckWorkspaceTests.cpp
    widgets/ReportPreviewRendererTests.cpp
    widgets/UiBuilderTests.cpp
    widgets/ReportDesignerWidgetTests.cpp
    widgets/TemplateDesignerWidgetTests.cpp
)

qt_add_executable(CardStackTests
    CardStackTestSuite.cpp
    support/VisualTestSupport.cpp
    ${CARDSTACK_TEST_SOURCES}
)

target_compile_definitions(CardStackTests
    PRIVATE
        CARDSTACK_TEST_SUITE
)

target_compile_options(CardStackTests
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/FS>
)

target_link_libraries(CardStackTests
    PRIVATE
        cardstack_app_lib
        cardstack_legacy_deck
        cardstack_btrieve
        cardstack_btrieve_filesaver
        cardstack_core
        cardstack_sqlite
        cardstack_widgets
        Qt6::Test
)

if(TARGET btrievefilesaver_gpl)
    target_link_libraries(CardStackTests PRIVATE btrievefilesaver_gpl)
    target_compile_definitions(CardStackTests PRIVATE CARDSTACK_TEST_NATIVE_BTRIEVE)
    add_executable(BtrieveMemoryInputTests "${PROJECT_SOURCE_DIR}/third_party/BtrieveFileSaver-Code/tests/memory_input_test.c")
    target_link_libraries(BtrieveMemoryInputTests PRIVATE btrievefilesaver_gpl)
    add_dependencies(CardStackTests BtrieveMemoryInputTests)
    add_test(NAME BtrieveMemoryInputTests COMMAND BtrieveMemoryInputTests "${CMAKE_CURRENT_BINARY_DIR}/memory-input-test.btr")
    set_tests_properties(BtrieveMemoryInputTests PROPERTIES TIMEOUT 10)
endif()

set_target_properties(CardStackTests PROPERTIES
    WIN32_EXECUTABLE OFF
)

set(CARDSTACK_TEST_SUITE_NAMES
    MainWindowActionTests
    MainWindowWorkflowTests
    FocusedAuditRegressionTests
    ImportSecurityTests
    DeckMergeTests
    DelimitedTextTests
    PhoneCallLogTests
    DeckTemplateTests
    LegacyDeckReaderTests
    LegacyInterchangeReaderTests
    LegacyReportReaderTests
    BtrieveAuditReaderTests
    SQLiteDeckStoreTests
    SQLitePackageStoreTests
    PhoneticSearchTests
    DeckWorkspaceTests
    ReportPreviewRendererTests
    UiBuilderTests
    ReportDesignerWidgetTests
    TemplateDesignerWidgetTests
)

foreach(cardstack_test_suite IN LISTS CARDSTACK_TEST_SUITE_NAMES)
    set(cardstack_suite_timeout_seconds 60)
    if(cardstack_test_suite STREQUAL "MainWindowWorkflowTests"
       OR cardstack_test_suite STREQUAL "SQLiteDeckStoreTests"
       OR cardstack_test_suite STREQUAL "SQLitePackageStoreTests")
        # These suites aggregate many independent durable saves and migrations.
        # Hosted Windows disk latency must not exhaust the total suite budget.
        # Keep the per-function 30-second hang guard below unchanged.
        set(cardstack_suite_timeout_seconds 180)
    endif()
    add_test(
        NAME ${cardstack_test_suite}
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:CardStackTests>
            -DTEST_SUITE=${cardstack_test_suite}
            -DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/${cardstack_test_suite}.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunQtTest.cmake
    )
    set_tests_properties(${cardstack_test_suite} PROPERTIES
        TIMEOUT ${cardstack_suite_timeout_seconds}
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins;QTEST_FUNCTION_TIMEOUT=30000"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;CARDSTACK_WINEVDM_GOLDEN_DIR=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR};CARDSTACK_LEGACY_DECK_SAMPLE=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR}/plain.BTN;CARDSTACK_LEGACY_REPORT_SAMPLE=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR}/reports.RPT"
    )
endforeach()

if(TARGET CardStack)
    add_dependencies(CardStackTests CardStack)
    add_test(NAME CardStackStartupSmoke COMMAND $<TARGET_FILE:CardStack> --smoke-test)
    set_tests_properties(CardStackStartupSmoke PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()
