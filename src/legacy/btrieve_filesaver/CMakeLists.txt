if(CARDSTACK_ENABLE_GPL_BTRIEVEFILESAVER)
    add_library(btrievefilesaver_gpl STATIC
        ${PROJECT_SOURCE_DIR}/third_party/BtrieveFileSaver-Code/BtrieveFileSaverLib.c
        ${PROJECT_SOURCE_DIR}/third_party/BtrieveFileSaver-Code/BtrieveFileSaverLib.h
    )

    target_include_directories(btrievefilesaver_gpl
        PUBLIC
            ${PROJECT_SOURCE_DIR}/third_party/BtrieveFileSaver-Code
    )

    qt_add_library(cardstack_btrieve_filesaver STATIC
        BtrieveFileSaverReader.cpp
        BtrieveFileSaverReader.h
    )

    target_link_libraries(cardstack_btrieve_filesaver
        PUBLIC
            Qt6::Core
            cardstack_btrieve
        PRIVATE
            btrievefilesaver_gpl
    )

    target_include_directories(cardstack_btrieve_filesaver
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(btrievefilesaver_dump EXCLUDE_FROM_ALL
        ${PROJECT_SOURCE_DIR}/third_party/BtrieveFileSaver-Code/main.c
        ${PROJECT_SOURCE_DIR}/third_party/BtrieveFileSaver-Code/BtrieveFileSaverLib.c
        ${PROJECT_SOURCE_DIR}/third_party/BtrieveFileSaver-Code/BtrieveFileSaverLib.h
    )

    target_include_directories(btrievefilesaver_dump
        PRIVATE
            ${PROJECT_SOURCE_DIR}/third_party/BtrieveFileSaver-Code
    )
else()
    add_library(cardstack_btrieve_filesaver INTERFACE)
endif()
