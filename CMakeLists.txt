cmake_minimum_required (VERSION 3.20)

project (
  "Lightning++"
  DESCRIPTION "A modern web framework for C++"
  HOMEPAGE_URL "https://github.com/c-carrasco/lightning++"
  LANGUAGES CXX
)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set (CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type options are: Debug, Release" FORCE)
endif()

include (cmake/configure_compiler.cmake)
include (cmake/configure_version.cmake)
include (CTest)
include (GNUInstallDirs)
option (LIGHTNING_USE_CONAN "Install dependencies using Conan" ON)
option (LIGHTNING_ENABLE_COVERAGE "Enable LLVM coverage and the coverage target (Debug Clang)" OFF)
if (LIGHTNING_USE_CONAN)
  include (cmake/conan.cmake)
endif()
include (cmake/doxygen.cmake)

set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

list (APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

find_package (asio CONFIG REQUIRED)
find_package (llhttp CONFIG REQUIRED)
if (BUILD_TESTING)
  find_package (GTest CONFIG REQUIRED)
endif()
find_package (Threads REQUIRED)
find_package (fmt CONFIG REQUIRED)
find_package (nlohmann_json CONFIG REQUIRED)

find_program (CCACHE_PROGRAM ccache)

if (CCACHE_PROGRAM)
  message (STATUS "Enabling ccache")
  set_property (GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
endif()

include(FetchContent)

set (CXXLOG_BUILD_TESTS OFF CACHE INTERNAL "")
# Use the logger's library target directly. Its top-level build runs a second,
# Conan 1-only dependency install into our build directory.
FetchContent_Declare(
    CxxLogger
    GIT_REPOSITORY https://github.com/c-carrasco/cxxlogger.git
    GIT_TAG 069fbfddd261b1e0c8fe555fe30e1aa71c167bdb
    SOURCE_SUBDIR src
)
FetchContent_MakeAvailable(CxxLogger)
target_compile_definitions (cxxlogger INTERFACE CXXLOG_USE_FMT_LIBRARY)

add_subdirectory (src)

option (LIGHTNING_BUILD_EXAMPLES "Build Lightning++ examples" OFF)
if (LIGHTNING_BUILD_EXAMPLES)
  add_subdirectory (examples)
endif()

include (cmake/install.cmake)
include (cmake/coverage.cmake)

option (LIGHTNING_BUILD_BENCHMARKS "Build optional performance benchmarks and HTTP fixture" OFF)
if (LIGHTNING_BUILD_BENCHMARKS)
  add_subdirectory (benchmarks)
endif()
