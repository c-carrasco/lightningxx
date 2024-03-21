file (GLOB CXX_FILES FILES *.cxx)

set (EXE_NAME "test_lightning")

add_executable (${EXE_NAME} ${CXX_FILES})

target_include_directories(${EXE_NAME} PRIVATE ${GTEST_INCLUDE_DIRECTORIES})

target_link_libraries(${EXE_NAME}
  lightning
  GTest::GTest
)

add_test (NAME ${EXE_NAME} COMMAND $<TARGET_FILE:${EXE_NAME}>)

add_dependencies (${EXE_NAME} CopyDataFolder)