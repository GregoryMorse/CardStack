qt_add_executable(BtrieveAuditReaderTests
    BtrieveAuditReaderTests.cpp
)

target_link_libraries(BtrieveAuditReaderTests
    PRIVATE
        cardstack_btrieve
        cardstack_btrieve_filesaver
        Qt6::Test
)

add_test(NAME BtrieveAuditReaderTests COMMAND BtrieveAuditReaderTests)
set_tests_properties(BtrieveAuditReaderTests PROPERTIES
    ENVIRONMENT_MODIFICATION "${CARDSTACK_LEGACY_TEST_ENV}"
)
