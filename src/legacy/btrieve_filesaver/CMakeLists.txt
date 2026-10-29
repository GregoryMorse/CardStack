if(CARDSTACK_ENABLE_GPL_BTRIEVEFILESAVER)
    # Build the pinned PR revision directly; keep one authoritative reader implementation.
    set(CARDSTACK_BTRIEVE_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/BtrieveFileSaver-Code")
    add_library(btrievefilesaver_gpl STATIC
        ${CARDSTACK_BTRIEVE_SOURCE_DIR}/BtrieveFileSaverLib.c
        ${CARDSTACK_BTRIEVE_SOURCE_DIR}/BtrieveFileSaverLib.h
    )

    target_include_directories(btrievefilesaver_gpl
        PUBLIC
            ${CARDSTACK_BTRIEVE_SOURCE_DIR}
    )

    qt_add_library(cardstack_btrieve_filesaver STATIC
        BtrieveFileSaverReader.cpp
        BtrieveFileSaverReader.h
    )

    target_link_libraries(cardstack_btrieve_filesaver
        PUBLIC
            Qt6::Core
        PRIVATE
            btrievefilesaver_gpl
    )

    target_include_directories(cardstack_btrieve_filesaver
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE
            ${CARDSTACK_BTRIEVE_SOURCE_DIR}
    )

    add_executable(btrievefilesaver_dump EXCLUDE_FROM_ALL
        ${PROJECT_SOURCE_DIR}/third_party/BtrieveFileSaver-Code/main.c
        ${CARDSTACK_BTRIEVE_SOURCE_DIR}/BtrieveFileSaverLib.c
        ${CARDSTACK_BTRIEVE_SOURCE_DIR}/BtrieveFileSaverLib.h
    )

    target_include_directories(btrievefilesaver_dump
        PRIVATE
            ${CARDSTACK_BTRIEVE_SOURCE_DIR}
    )
else()
    add_library(cardstack_btrieve_filesaver INTERFACE)
endif()
