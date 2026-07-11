qt_add_library(cardstack_app_lib STATIC
    MainWindow.cpp
    MainWindow.h
)

qt6_add_resources(cardstack_app_resource_sources
    cardstack_app.qrc
)

target_sources(cardstack_app_lib
    PRIVATE
        ${cardstack_app_resource_sources}
)

target_link_libraries(cardstack_app_lib
    PUBLIC
        cardstack_legacy_deck
        cardstack_btrieve_filesaver
        cardstack_core
        cardstack_sqlite
        cardstack_widgets
        Qt6::PrintSupport
        Qt6::Widgets
)

target_include_directories(cardstack_app_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

qt_add_executable(CardStack
    main.cpp
)

target_link_libraries(CardStack
    PRIVATE
        cardstack_app_lib
        Qt6::Widgets
)

option(CARDSTACK_DEPLOY_QT_TRANSLATIONS "Deploy Qt runtime translation catalogs with windeployqt." OFF)
option(CARDSTACK_DEPLOY_QT_AFTER_BUILD "Run windeployqt automatically after building CardStack on Windows." ON)
set(CARDSTACK_WINDEPLOYQT_MODE "auto" CACHE STRING "windeployqt mode: auto, debug, or release")
set_property(CACHE CARDSTACK_WINDEPLOYQT_MODE PROPERTY STRINGS auto debug release)

if(WIN32)
    target_sources(CardStack PRIVATE app_icon.rc)
endif()

set_target_properties(CardStack PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

if(WIN32)
    set(_cardstack_windeployqt "")
    if(TARGET Qt6::windeployqt)
        get_target_property(_cardstack_windeployqt Qt6::windeployqt IMPORTED_LOCATION)
    endif()

    if(NOT _cardstack_windeployqt AND TARGET Qt6::qmake)
        get_target_property(_cardstack_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_cardstack_qt_bin_dir "${_cardstack_qmake}" DIRECTORY)
        find_program(_cardstack_windeployqt
            NAMES windeployqt windeployqt6
            HINTS "${_cardstack_qt_bin_dir}"
            NO_DEFAULT_PATH
        )
    endif()

    if(NOT _cardstack_windeployqt)
        find_program(_cardstack_windeployqt NAMES windeployqt windeployqt6)
    endif()

    if(_cardstack_windeployqt)
        set(_cardstack_deploy_dir "$<TARGET_FILE_DIR:CardStack>/CardStackDeploy")
        set(_cardstack_deployed_exe "${_cardstack_deploy_dir}/$<TARGET_FILE_NAME:CardStack>")
        set(_cardstack_no_translations FALSE)
        if(NOT CARDSTACK_DEPLOY_QT_TRANSLATIONS)
            set(_cardstack_no_translations TRUE)
        endif()

        set(_cardstack_deploy_command
            ${CMAKE_COMMAND}
            "-DDEPLOY_DIR=${_cardstack_deploy_dir}"
            "-DSOURCE_EXE=$<TARGET_FILE:CardStack>"
            "-DDEPLOYED_EXE=${_cardstack_deployed_exe}"
            "-DWINDEPLOYQT=${_cardstack_windeployqt}"
            "-DDEPLOY_MODE=${CARDSTACK_WINDEPLOYQT_MODE}"
            "-DNO_TRANSLATIONS=${_cardstack_no_translations}"
            -P "${PROJECT_SOURCE_DIR}/scripts/deploy-cardstack-windows.cmake"
        )

        add_custom_target(CardStackDeploy
            COMMAND ${_cardstack_deploy_command}
            DEPENDS CardStack
            COMMENT "Deploying Qt runtime beside CardStack"
            VERBATIM
        )

        if(CARDSTACK_DEPLOY_QT_AFTER_BUILD)
            add_custom_command(TARGET CardStack POST_BUILD
                COMMAND ${_cardstack_deploy_command}
                COMMENT "Deploying Qt runtime beside CardStack"
                VERBATIM
            )
        endif()
    else()
        message(WARNING "windeployqt was not found. CardStack.exe may require Qt DLLs on PATH; set Qt6_DIR/CMAKE_PREFIX_PATH to a full Qt install or build CardStackDeploy after windeployqt is available.")
    endif()
endif()
