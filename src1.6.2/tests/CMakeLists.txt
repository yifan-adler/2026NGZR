cmake_minimum_required(VERSION 3.10)
project(rdfw_local_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
get_filename_component(SOURCE_DIR ".." ABSOLUTE)
file(READ "${SOURCE_DIR}/words.txt" TEST_WORDS)
string(REPLACE "\r\n" "\n" TEST_WORDS "${TEST_WORDS}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/words.txt" "${TEST_WORDS}")
add_library(rdfw_test_core STATIC
    ../rdfw.cpp ../parser.cpp ../deadline_manager.cpp ../terminal_checker.cpp
    ../score_evaluator.cpp ../candidate_plan.cpp ../task_group_search.cpp ../question_preflight.cpp)
target_include_directories(rdfw_test_core PUBLIC stubs "${SOURCE_DIR}")
foreach(test_name input_safety_tests three_a_tests question_preflight_tests legal_preservation_tests recovery_evidence_tests task_group_search_tests task_group_projection_tests guarded_decision_tests score_semantics_tests)
    add_executable(${test_name} "${test_name}.cpp")
    target_link_libraries(${test_name} rdfw_test_core)
    # These fixtures use assert, including setup expressions.
    target_compile_options(${test_name} PRIVATE -UNDEBUG)
    add_test(NAME ${test_name} COMMAND ${test_name} "${CMAKE_CURRENT_BINARY_DIR}/words.txt")
endforeach()

if(OFFICIAL_SDK)
    add_executable(official_score_semantics_tests score_semantics_tests.cpp)
    target_compile_definitions(official_score_semantics_tests PRIVATE OFFICIAL_SCORE_TEST)
    target_compile_options(official_score_semantics_tests PRIVATE -UNDEBUG)
    target_include_directories(official_score_semantics_tests BEFORE PRIVATE stubs "${SOURCE_DIR}" "${OFFICIAL_SDK}/src" "${OFFICIAL_SDK}/include")
    target_link_libraries(official_score_semantics_tests rdfw_test_core "${OFFICIAL_SDK}/lib/libasp.so")
    set(SDK_TEST_WORK "${CMAKE_CURRENT_BINARY_DIR}/official-semantics")
    file(MAKE_DIRECTORY "${SDK_TEST_WORK}")
    file(GLOB SDK_TEST_LP "${OFFICIAL_SDK}/res/*.lp")
    file(COPY ${SDK_TEST_LP} "${OFFICIAL_SDK}/res/iclingo"
        "${OFFICIAL_SDK}/bin/vrunact.sh" "${OFFICIAL_SDK}/bin/vruntask.sh"
        DESTINATION "${SDK_TEST_WORK}")
    execute_process(COMMAND chmod +x "${SDK_TEST_WORK}/iclingo"
        "${SDK_TEST_WORK}/vrunact.sh" "${SDK_TEST_WORK}/vruntask.sh")
    add_test(NAME official_score_semantics_tests COMMAND official_score_semantics_tests "${CMAKE_CURRENT_BINARY_DIR}/words.txt")
    set_tests_properties(official_score_semantics_tests PROPERTIES WORKING_DIRECTORY "${SDK_TEST_WORK}")
endif()
