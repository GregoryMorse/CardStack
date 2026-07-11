qt_add_library(cardstack_widgets STATIC
    CardDetailPanel.cpp
    CardDetailPanel.h
    CardTableModel.cpp
    CardTableModel.h
    DeckWorkspace.cpp
    DeckWorkspace.h
    PhoneticSearch.cpp
    PhoneticSearch.h
    UiResourceData.cpp
    UiResourceData.h
    UiIds.h
    UiBuilder.cpp
    UiBuilder.h
    ReportDesignerWidget.cpp
    ReportDesignerWidget.h
    ReportPreviewDialog.cpp
    ReportPreviewDialog.h
    ReportPrintEngine.cpp
    ReportPrintEngine.h
    ReportPreviewRenderer.cpp
    ReportPreviewRenderer.h
    TemplateDesignerWidget.cpp
    TemplateDesignerWidget.h
)

target_link_libraries(cardstack_widgets
    PUBLIC
        cardstack_core
        Qt6::Widgets
)

target_include_directories(cardstack_widgets
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
