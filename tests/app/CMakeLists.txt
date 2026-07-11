qt_add_executable(MainWindowActionTests
    MainWindowActionTests.cpp
)

target_link_libraries(MainWindowActionTests
    PRIVATE
        cardstack_app_lib
        Qt6::Test
)

add_test(NAME MainWindowActionTests COMMAND MainWindowActionTests)
set_tests_properties(MainWindowActionTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
