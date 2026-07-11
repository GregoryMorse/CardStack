qt_add_library(cardstack_sqlite STATIC
    SQLiteDeckRepository.cpp
    SQLiteDeckRepository.h
    SQLitePackageStore.cpp
    SQLitePackageStore.h
    SQLiteDeckStore.cpp
    SQLiteDeckStore.h
)

target_link_libraries(cardstack_sqlite
    PUBLIC
        cardstack_core
        Qt6::Sql
)

target_include_directories(cardstack_sqlite
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
