find_program (CONAN_TOOL conan REQUIRED)

execute_process (
  COMMAND "${CONAN_TOOL}" --version
  OUTPUT_VARIABLE CONAN_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY
)

execute_process (
  COMMAND "${CONAN_TOOL}" profile path default
  RESULT_VARIABLE CONAN_PROFILE_RESULT
  OUTPUT_QUIET
  ERROR_QUIET
)

if (NOT CONAN_PROFILE_RESULT EQUAL 0)
  if (CONAN_VERSION MATCHES "Conan version 2\\.")
    set (CONAN_DETECT_COMMAND profile detect)
  else()
    set (CONAN_DETECT_COMMAND profile new default --detect)
  endif()
  execute_process (
    COMMAND "${CONAN_TOOL}" ${CONAN_DETECT_COMMAND}
    COMMAND_ERROR_IS_FATAL ANY
  )
endif()

execute_process (
  COMMAND "${CONAN_TOOL}" install
    --profile:build=default
    --profile:host=default
    -s build_type=${CMAKE_BUILD_TYPE}
    -s compiler=${COMPILER_NAME}
    -s compiler.version=${COMPILER_VERSION}
    -s compiler.libcxx=${COMPILER_LIBCXX}
    -s compiler.cppstd=${CMAKE_CXX_STANDARD}
    --build=missing
    "--output-folder=${CMAKE_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}"
  COMMAND_ERROR_IS_FATAL ANY
)

list (PREPEND CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}")
list (PREPEND CMAKE_MODULE_PATH "${CMAKE_BINARY_DIR}")
