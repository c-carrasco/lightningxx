# This dependency is intentionally absent from ordinary library builds/exports.
set (BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set (BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set (BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_Declare (googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.1 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable (googlebenchmark)

execute_process (COMMAND git rev-parse HEAD WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  OUTPUT_VARIABLE BENCH_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process (COMMAND git status --porcelain WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  OUTPUT_VARIABLE BENCH_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE)
set (BENCH_DIRTY false)
if (BENCH_STATUS)
  set (BENCH_DIRTY true)
endif()
string (TOUPPER "${CMAKE_BUILD_TYPE}" bench_config)
set (BENCH_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${bench_config}}")
set (BENCH_TIMING_ENABLED false)
if (CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT CMAKE_CONFIGURATION_TYPES
    AND NOT ENABLE_ASAN AND NOT ENABLE_UBSAN AND NOT ENABLE_TSAN
    AND NOT LIGHTNING_ENABLE_COVERAGE AND NOT BENCH_FLAGS MATCHES "sanitize|coverage|profile")
  set (BENCH_TIMING_ENABLED true)
endif()
configure_file (build_info.h.in build_info.h @ONLY)

add_executable (bench_lightning micro.cxx)
target_include_directories (bench_lightning PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}/src/lib")
target_link_libraries (bench_lightning PRIVATE lightning::lightning benchmark::benchmark)
add_executable (bench_http_server server.cxx)
target_include_directories (bench_http_server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries (bench_http_server PRIVATE lightning::lightning)

if (BUILD_TESTING)
  find_package (Python3 3.9 REQUIRED COMPONENTS Interpreter)
  add_test (NAME benchmark_micro_smoke COMMAND bench_lightning --smoke
    "--benchmark_out=${CMAKE_CURRENT_BINARY_DIR}/smoke.json" --benchmark_out_format=json)
  add_test (NAME benchmark_harness COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_harness.py" --server "$<TARGET_FILE:bench_http_server>")
  set_tests_properties (benchmark_micro_smoke benchmark_harness PROPERTIES TIMEOUT 60 LABELS benchmark)
endif()
