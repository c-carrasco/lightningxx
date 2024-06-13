FetchContent_Declare(
  cpp-httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
)
FetchContent_MakeAvailable (cpp-httplib)

file (GLOB CXX_FILES FILES *.cxx)

set (EXE_NAME "test_lightning")

add_executable (${EXE_NAME} ${CXX_FILES})

target_include_directories(${EXE_NAME} PRIVATE ${GTEST_INCLUDE_DIRECTORIES})

target_link_libraries(${EXE_NAME}
  lightning
  GTest::GTest
  httplib::httplib
)

add_test (NAME ${EXE_NAME} COMMAND $<TARGET_FILE:${EXE_NAME}>)

add_dependencies (${EXE_NAME} CopyDataFolder)