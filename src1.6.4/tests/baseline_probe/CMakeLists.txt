cmake_minimum_required(VERSION 3.10)
project(state_invariant_baseline_probe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
enable_testing()
get_filename_component(BASELINE "../../../src1.6.3" ABSOLUTE)
get_filename_component(CURRENT "../.." ABSOLUTE)
# Compile unchanged 1.6.3 with the controllable test-only platform stub.
add_library(probe_core STATIC
    ${BASELINE}/rdfw.cpp ${BASELINE}/parser.cpp ${BASELINE}/deadline_manager.cpp
    ${BASELINE}/terminal_checker.cpp ${BASELINE}/legacy_priority.cpp
    ${BASELINE}/score_evaluator.cpp ${BASELINE}/candidate_plan.cpp
    ${BASELINE}/task_group_search.cpp ${BASELINE}/question_preflight.cpp)
target_include_directories(probe_core PUBLIC ${CURRENT}/tests/stubs ${BASELINE})
add_executable(state_invariant_tests ../state_invariant_tests.cpp)
target_link_libraries(state_invariant_tests probe_core)
target_compile_options(state_invariant_tests PRIVATE -UNDEBUG)
file(READ "${BASELINE}/words.txt" WORDS)
string(REPLACE "\r\n" "\n" WORDS "${WORDS}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/words.txt" "${WORDS}")
foreach(case RANGE 0 19)
    add_test(NAME baseline_invariant_${case} COMMAND state_invariant_tests "${CMAKE_CURRENT_BINARY_DIR}/words.txt" ${case})
    set_tests_properties(baseline_invariant_${case} PROPERTIES TIMEOUT 5)
endforeach()
