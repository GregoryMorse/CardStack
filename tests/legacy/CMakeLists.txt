set(CARDSTACK_WINEVDM_TEST_FIXTURE_DIR
    "${CMAKE_SOURCE_DIR}/tests/fixtures/legacy/winevdm"
)
set(CARDSTACK_LEGACY_TEST_ENV
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    "CARDSTACK_WINEVDM_GOLDEN_DIR=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR}"
    "CARDSTACK_LEGACY_DECK_SAMPLE=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR}/plain.BTN"
    "CARDSTACK_LEGACY_REPORT_SAMPLE=set:${CARDSTACK_WINEVDM_TEST_FIXTURE_DIR}/reports.RPT"
)

add_subdirectory(btrieve)

qt_add_executable(LegacyDeckReaderTests
    LegacyDeckReaderTests.cpp
)

target_link_libraries(LegacyDeckReaderTests
    PRIVATE
        cardstack_legacy_deck
        Qt6::Test
)

add_test(NAME LegacyDeckReaderTests COMMAND LegacyDeckReaderTests)
set_tests_properties(LegacyDeckReaderTests PROPERTIES
    ENVIRONMENT_MODIFICATION "${CARDSTACK_LEGACY_TEST_ENV}"
)

qt_add_executable(LegacyInterchangeReaderTests
    LegacyInterchangeReaderTests.cpp
)

target_link_libraries(LegacyInterchangeReaderTests
    PRIVATE
        cardstack_legacy_deck
        Qt6::Test
)

add_test(NAME LegacyInterchangeReaderTests COMMAND LegacyInterchangeReaderTests)
set_tests_properties(LegacyInterchangeReaderTests PROPERTIES
    ENVIRONMENT_MODIFICATION "${CARDSTACK_LEGACY_TEST_ENV}"
)

qt_add_executable(LegacyReportReaderTests
    LegacyReportReaderTests.cpp
)

target_link_libraries(LegacyReportReaderTests
    PRIVATE
        cardstack_legacy_deck
        Qt6::Test
)

add_test(NAME LegacyReportReaderTests COMMAND LegacyReportReaderTests)
set_tests_properties(LegacyReportReaderTests PROPERTIES
    ENVIRONMENT_MODIFICATION "${CARDSTACK_LEGACY_TEST_ENV}"
)
