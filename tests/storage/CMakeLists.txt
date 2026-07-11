qt_add_executable(SQLiteDeckStoreTests
    SQLiteDeckStoreTests.cpp
)

target_link_libraries(SQLiteDeckStoreTests
    PRIVATE
        cardstack_sqlite
        Qt6::Test
)

add_test(NAME SQLiteDeckStoreTests COMMAND SQLiteDeckStoreTests)
set_tests_properties(SQLiteDeckStoreTests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(SQLitePackageStoreTests
    SQLitePackageStoreTests.cpp
)

target_link_libraries(SQLitePackageStoreTests
    PRIVATE
        cardstack_sqlite
        Qt6::Test
)

add_test(NAME SQLitePackageStoreTests COMMAND SQLitePackageStoreTests)
set_tests_properties(SQLitePackageStoreTests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
