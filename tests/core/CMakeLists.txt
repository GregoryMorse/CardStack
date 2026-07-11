qt_add_executable(DeckMergeTests
    DeckMergeTests.cpp
)

target_link_libraries(DeckMergeTests
    PRIVATE
        cardstack_core
        Qt6::Test
)

add_test(NAME DeckMergeTests COMMAND DeckMergeTests)
set_tests_properties(DeckMergeTests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(DelimitedTextTests
    DelimitedTextTests.cpp
)

target_link_libraries(DelimitedTextTests
    PRIVATE
        cardstack_core
        Qt6::Test
)

add_test(NAME DelimitedTextTests COMMAND DelimitedTextTests)
set_tests_properties(DelimitedTextTests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(DeckTemplateTests
    DeckTemplateTests.cpp
)

target_link_libraries(DeckTemplateTests
    PRIVATE
        cardstack_core
        Qt6::Test
)

add_test(NAME DeckTemplateTests COMMAND DeckTemplateTests)
set_tests_properties(DeckTemplateTests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
