cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED DEPLOY_DIR OR NOT DEFINED SOURCE_EXE OR NOT DEFINED DEPLOYED_EXE OR NOT DEFINED WINDEPLOYQT)
    message(FATAL_ERROR "DEPLOY_DIR, SOURCE_EXE, DEPLOYED_EXE, and WINDEPLOYQT are required.")
endif()

file(MAKE_DIRECTORY "${DEPLOY_DIR}")
file(COPY_FILE "${SOURCE_EXE}" "${DEPLOYED_EXE}" ONLY_IF_DIFFERENT RESULT copy_result)
if(NOT copy_result STREQUAL "0")
    message(WARNING "Could not update deployed CardStack executable: ${copy_result}. Close any running copy of ${DEPLOYED_EXE} and rebuild to refresh the deployed test build.")
    return()
endif()

set(windeployqt_args "${WINDEPLOYQT}")
if(DEPLOY_MODE STREQUAL "debug")
    list(APPEND windeployqt_args --debug)
elseif(DEPLOY_MODE STREQUAL "release")
    list(APPEND windeployqt_args --release)
endif()

if(NO_TRANSLATIONS)
    list(APPEND windeployqt_args --no-translations)
endif()

list(APPEND windeployqt_args --dir "${DEPLOY_DIR}" "${DEPLOYED_EXE}")
execute_process(COMMAND ${windeployqt_args} RESULT_VARIABLE deploy_result)
if(NOT deploy_result EQUAL 0)
    message(WARNING "windeployqt failed with exit code ${deploy_result}. The app build succeeded, but the deploy folder may be incomplete.")
endif()
