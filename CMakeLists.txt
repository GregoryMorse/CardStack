cmake_minimum_required(VERSION 3.24)

project(CardStack
    VERSION 0.1.0
    DESCRIPTION "Modern open-source card database"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql Test PrintSupport)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS LinguistTools)

qt_standard_project_setup()
include(CTest)

option(CARDSTACK_ENABLE_GPL_BTRIEVEFILESAVER "Build GPL BtrieveFileSaver migration support" ON)
option(CARDSTACK_DEPLOY_QT_AFTER_BUILD "Run Qt deployment tooling after building the app on Windows" ON)
option(CARDSTACK_ENABLE_TRANSLATIONS "Enable Qt Linguist extraction/release targets when Qt Linguist tools are available" ON)
set(CARDSTACK_WINDEPLOYQT_MODE "release" CACHE STRING "windeployqt runtime mode: release, debug, or auto")
set_property(CACHE CARDSTACK_WINDEPLOYQT_MODE PROPERTY STRINGS release debug auto)

add_subdirectory(src/core)
add_subdirectory(src/legacy/btrieve)
add_subdirectory(src/legacy/btrieve_filesaver)
add_subdirectory(src/legacy/legacy_deck)
add_subdirectory(src/storage/sqlite)
add_subdirectory(src/ui/widgets)
add_subdirectory(src/app)

if(CARDSTACK_ENABLE_TRANSLATIONS)
    set(CARDSTACK_TRANSLATION_FILES
        "${PROJECT_SOURCE_DIR}/translations/cardstack_en.ts"
    )

    if(Qt6LinguistTools_FOUND)
        qt_add_translations(CardStack
            TS_FILES ${CARDSTACK_TRANSLATION_FILES}
            RESOURCE_PREFIX "/cardstack/i18n"
        )
    else()
        add_custom_target(CardStackTranslations
            COMMAND ${CMAKE_COMMAND} -E echo "Qt Linguist tools were not found; install/build qttools to enable translation extraction and release targets."
            VERBATIM
        )
    endif()
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
