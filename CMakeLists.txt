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

add_subdirectory (src)