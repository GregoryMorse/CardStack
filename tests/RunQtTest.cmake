if(NOT DEFINED TEST_EXECUTABLE OR NOT DEFINED TEST_SUITE OR NOT DEFINED TEST_LOG)
    message(FATAL_ERROR "TEST_EXECUTABLE, TEST_SUITE, and TEST_LOG are required.")
endif()

execute_process(
    COMMAND "${TEST_EXECUTABLE}" --test "${TEST_SUITE}" -o "${TEST_LOG},txt"
    RESULT_VARIABLE test_result
)

if(NOT test_result EQUAL 0)
    if(EXISTS "${TEST_LOG}")
        file(READ "${TEST_LOG}" test_output)
        message("${test_output}")
    endif()
    message(FATAL_ERROR "${TEST_SUITE} failed with exit code ${test_result}.")
endif()
