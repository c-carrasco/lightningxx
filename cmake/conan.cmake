find_program (CONAN_TOOL conan REQUIRED)

execute_process (
  COMMAND conan install
    -s build_type=Release
    -s compiler=${COMPILER_NAME}
    -s compiler.version=${COMPILER_VERSION}
    -s compiler.libcxx=${COMPILER_LIBCXX}
    --build=missing
    --output-folder=${CMAKE_BINARY_DIR}
    ${PROJECT_SOURCE_DIR}
)