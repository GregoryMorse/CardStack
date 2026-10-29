cmake_minimum_required(VERSION 3.24)

project(CardStack
    VERSION 1.1.0
    DESCRIPTION "Modern open-source card database"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

option(CARDSTACK_ENABLE_SANITIZERS "Instrument application, importers and tests for memory errors" OFF)
if(CARDSTACK_ENABLE_SANITIZERS)
    if(MSVC)
        add_compile_options(/fsanitize=address /Zi)
        add_link_options(/INCREMENTAL:NO /DEBUG)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "Sanitizers are not configured for this compiler")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql Test PrintSupport Svg)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS LinguistTools)

qt_standard_project_setup()
include(CTest)

# Pin the small cryptographic implementation used for encrypted deck containers.
include(FetchContent)
FetchContent_Declare(cardstack_monocypher
    URL https://monocypher.org/download/monocypher-4.0.3.tar.gz
    URL_HASH SHA512=40904ada5c7ee4f7741733e38b69a30a4b0561cbffba5ffe7c2dce16136d540251ec0d9056ff606510d3b5b708fb8a40db7e0870d4a0b2dc17ba2bfb880f8965
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(cardstack_monocypher)
add_library(cardstack_crypto STATIC "${cardstack_monocypher_SOURCE_DIR}/src/monocypher.c")
target_include_directories(cardstack_crypto SYSTEM PUBLIC "${cardstack_monocypher_SOURCE_DIR}/src")
set_target_properties(cardstack_crypto PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

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
