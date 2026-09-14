if (NOT LIGHTNING_ENABLE_COVERAGE)
  return()
endif()
if (NOT BUILD_TESTING OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"
    OR NOT CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
  message (FATAL_ERROR "Coverage requires BUILD_TESTING=ON and a single-config Debug Clang/AppleClang build")
endif()
if (ENABLE_ASAN OR ENABLE_UBSAN OR ENABLE_TSAN)
  message (FATAL_ERROR "Run coverage and sanitizers in separate builds")
endif()
find_package (Python3 3.9 REQUIRED COMPONENTS Interpreter)
get_filename_component (compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
if (APPLE)
  execute_process (COMMAND xcrun --find llvm-cov OUTPUT_VARIABLE xcode_llvm_cov
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  get_filename_component (compiler_dir "${xcode_llvm_cov}" DIRECTORY)
endif()
find_program (LIGHTNING_LLVM_COV NAMES "llvm-cov-${COMPILER_VERSION_MAJOR}" llvm-cov
  HINTS "${compiler_dir}" REQUIRED)
find_program (LIGHTNING_LLVM_PROFDATA NAMES "llvm-profdata-${COMPILER_VERSION_MAJOR}" llvm-profdata
  HINTS "${compiler_dir}" REQUIRED)
set (LIGHTNING_COVERAGE_MIN_LINES 90 CACHE STRING "Minimum project line coverage percent")
set (LIGHTNING_COVERAGE_MIN_BRANCHES 85 CACHE STRING "Minimum project branch coverage percent")
target_compile_options (lightning PUBLIC
  "$<BUILD_INTERFACE:-fprofile-instr-generate>" "$<BUILD_INTERFACE:-fcoverage-mapping>")
target_link_options (lightning PUBLIC "$<BUILD_INTERFACE:-fprofile-instr-generate>")
add_custom_target (coverage
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/coverage.py"
    --binary "$<TARGET_FILE:test_lightning>" --source "${PROJECT_SOURCE_DIR}"
    --output "${PROJECT_BINARY_DIR}/coverage" --llvm-cov "${LIGHTNING_LLVM_COV}"
    --llvm-profdata "${LIGHTNING_LLVM_PROFDATA}"
    --min-lines "${LIGHTNING_COVERAGE_MIN_LINES}" --min-branches "${LIGHTNING_COVERAGE_MIN_BRANCHES}"
  DEPENDS test_lightning USES_TERMINAL VERBATIM)
add_test (NAME coverage_tool COMMAND "${Python3_EXECUTABLE}"
  "${PROJECT_SOURCE_DIR}/tools/test_coverage.py")
set_tests_properties (coverage_tool PROPERTIES TIMEOUT 10)
