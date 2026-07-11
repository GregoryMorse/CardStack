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

add_test(NAME CardStackTests COMMAND CardStackTests)
set_tests_properties(CardStackTests PROPERTIES
    ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;CARDSTACK_WINEVDM_GOLDEN_DIR=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR};CARDSTACK_LEGACY_DECK_SAMPLE=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR}/plain.BTN;CARDSTACK_LEGACY_REPORT_SAMPLE=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR}/reports.RPT"
)
