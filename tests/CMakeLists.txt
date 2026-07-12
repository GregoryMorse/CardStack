set(CARDSTACK_WINEVDM_TEST_FIXTURE_DIR
    "${CMAKE_SOURCE_DIR}/tests/fixtures/legacy/winevdm"
)

set(CARDSTACK_TEST_SOURCES
    app/MainWindowActionTests.cpp
    app/MainWindowWorkflowTests.cpp
    core/DeckMergeTests.cpp
    core/DelimitedTextTests.cpp
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

set_target_properties(CardStackTests PROPERTIES
    WIN32_EXECUTABLE OFF
)

set(CARDSTACK_TEST_SUITE_NAMES
    MainWindowActionTests
    MainWindowWorkflowTests
    DeckMergeTests
    DelimitedTextTests
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
    add_test(
        NAME ${cardstack_test_suite}
        COMMAND ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:CardStackTests>
            -DTEST_SUITE=${cardstack_test_suite}
            -DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/${cardstack_test_suite}.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunQtTest.cmake
    )
    set_tests_properties(${cardstack_test_suite} PROPERTIES
        TIMEOUT 60
        ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins;QTEST_FUNCTION_TIMEOUT=30000"
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;CARDSTACK_WINEVDM_GOLDEN_DIR=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR};CARDSTACK_LEGACY_DECK_SAMPLE=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR}/plain.BTN;CARDSTACK_LEGACY_REPORT_SAMPLE=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR}/reports.RPT"
    )
endforeach()
