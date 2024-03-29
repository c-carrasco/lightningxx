set(CPP20_HTTP_CLIENT_BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(CPP20_HTTP_CLIENT_BUILD_TESTS OFF CACHE INTERNAL "")

FetchContent_Declare(
  Cpp20HttpClient
  GIT_REPOSITORY https://github.com/avocadoboi/cpp20-http-client.git
)
FetchContent_MakeAvailable(Cpp20HttpClient)
target_compile_options (cpp20_http_client PRIVATE "-Wno-braced-scalar-init")
target_compile_options (cpp20_http_client PUBLIC "-Wno-logical-op-parentheses")


file (GLOB CXX_FILES FILES *.cxx)

set (EXE_NAME "test_lightning")

add_executable (${EXE_NAME} ${CXX_FILES})

target_include_directories(${EXE_NAME} PRIVATE ${GTEST_INCLUDE_DIRECTORIES})

target_link_libraries(${EXE_NAME}
  lightning
  GTest::GTest
  Cpp20HttpClient::cpp20_http_client
)

add_test (NAME ${EXE_NAME} COMMAND $<TARGET_FILE:${EXE_NAME}>)

add_dependencies (${EXE_NAME} CopyDataFolder)