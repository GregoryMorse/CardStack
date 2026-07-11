qt_add_executable(PhoneticSearchTests
    PhoneticSearchTests.cpp
)

target_link_libraries(PhoneticSearchTests
    PRIVATE
        cardstack_widgets
        Qt6::Test
)

add_test(NAME PhoneticSearchTests COMMAND PhoneticSearchTests)
set_tests_properties(PhoneticSearchTests PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(DeckWorkspaceTests
    DeckWorkspaceTests.cpp
)

target_link_libraries(DeckWorkspaceTests
    PRIVATE
        cardstack_widgets
        Qt6::Test
)

add_test(NAME DeckWorkspaceTests COMMAND DeckWorkspaceTests)
set_tests_properties(DeckWorkspaceTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(ReportPreviewRendererTests
    ReportPreviewRendererTests.cpp
)

target_link_libraries(ReportPreviewRendererTests
    PRIVATE
        cardstack_widgets
        cardstack_legacy_deck
        Qt6::Test
)

add_test(NAME ReportPreviewRendererTests COMMAND ReportPreviewRendererTests)
set_tests_properties(ReportPreviewRendererTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(UiBuilderTests
    UiBuilderTests.cpp
)

target_link_libraries(UiBuilderTests
    PRIVATE
        cardstack_widgets
        Qt6::Test
)

add_test(NAME UiBuilderTests COMMAND UiBuilderTests)
set_tests_properties(UiBuilderTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(ReportDesignerWidgetTests
    ReportDesignerWidgetTests.cpp
)

target_link_libraries(ReportDesignerWidgetTests
    PRIVATE
        cardstack_widgets
        Qt6::Test
)

add_test(NAME ReportDesignerWidgetTests COMMAND ReportDesignerWidgetTests)
set_tests_properties(ReportDesignerWidgetTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(TemplateDesignerWidgetTests
    TemplateDesignerWidgetTests.cpp
)

target_link_libraries(TemplateDesignerWidgetTests
    PRIVATE
        cardstack_widgets
        Qt6::Test
)

add_test(NAME TemplateDesignerWidgetTests COMMAND TemplateDesignerWidgetTests)
set_tests_properties(TemplateDesignerWidgetTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
