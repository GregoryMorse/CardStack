qt_add_library(cardstack_core STATIC
    BuiltInReportTemplates.cpp
    BuiltInReportTemplates.h
    CardRecord.cpp
    CardRecord.h
    CardTemplateLayout.h
    DelimitedText.cpp
    DelimitedText.h
    DeckMerge.cpp
    DeckMerge.h
    DeckTemplate.cpp
    DeckTemplate.h
    Deck.cpp
    Deck.h
    FieldDefinition.cpp
    FieldDefinition.h
    ImportExportProfile.h
    PhoneCallLog.cpp
    PhoneCallLog.h
    ReportDefinition.cpp
    ReportDefinition.h
)

target_link_libraries(cardstack_core
    PUBLIC
        Qt6::Core
)

target_include_directories(cardstack_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
