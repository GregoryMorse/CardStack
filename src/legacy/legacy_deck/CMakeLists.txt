qt_add_library(cardstack_legacy_deck STATIC
    LegacyDeckReader.cpp
    LegacyDeckReader.h
    LegacyInterchangeReader.cpp
    LegacyInterchangeReader.h
    LegacyReportReader.cpp
    LegacyReportReader.h
)

target_link_libraries(cardstack_legacy_deck
    PUBLIC
        cardstack_core
        cardstack_btrieve
        cardstack_btrieve_filesaver
)

target_include_directories(cardstack_legacy_deck
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
