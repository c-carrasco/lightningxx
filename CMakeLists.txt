cmake_minimum_required (VERSION 3.20)

project (
  "Lightning++"
  DESCRIPTION "A modern web framework for C++"
  HOMEPAGE_URL "https://github.com/c-carrasco/lightning++"
  LANGUAGES CXX
)

include (cmake/configure_compiler.cmake)
include (cmake/configure_version.cmake)
include (cmake/conan.cmake)
include (cmake/doxygen.cmake)

set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set (CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type options are: Debug, Release")

list (APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

find_package (asio REQUIRED)
find_package (llhttp REQUIRED)
find_package (GTest REQUIRED)

find_program (CCACHE_PROGRAM ccache)

if (CCACHE_PROGRAM)
  message (STATUS "Enabling ccache")
  set_property (GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

enable_testing()

include (CTest)

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  add_custom_target(CopyDataFolder ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
  )
else()
  add_custom_target(CopyDataFolder ALL
    COMMAND rm -f ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
    COMMAND ln -s ${CMAKE_CURRENT_SOURCE_DIR}/data ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data
  )
endif()

include(FetchContent)

set (CXXLOG_BUILD_TESTS OFF CACHE INTERNAL "")
set (CXXLOG_DOCUMENTATION OFF CACHE INTERNAL "")

if (NOT SUPPORT_STD_FORMAT OR CMAKE_CXX_STANDARD LESS_EQUAL 17)
  add_compile_definitions(CXXLOG_USE_FMT_LIBRARY)
endif()

FetchContent_Declare(
    CxxLogger
    GIT_REPOSITORY https://github.com/c-carrasco/cxxlogger.git
    GIT_TAG main
)
FetchContent_MakeAvailable(CxxLogger)

add_subdirectory (src)