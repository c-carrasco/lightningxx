file (GLOB CXX_FILES FILES *.cxx)

set (EXE_NAME "test_lightning")

add_executable (${EXE_NAME} ${CXX_FILES})

target_link_libraries(${EXE_NAME}
  lightning
  GTest::gtest
)

add_test (NAME ${EXE_NAME} COMMAND $<TARGET_FILE:${EXE_NAME}>)
