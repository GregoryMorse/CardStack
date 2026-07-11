qt_add_library(cardstack_btrieve STATIC
    BtrieveAuditReader.cpp
    BtrieveAuditReader.h
)

target_link_libraries(cardstack_btrieve
    PUBLIC
        Qt6::Core
)

target_include_directories(cardstack_btrieve
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
