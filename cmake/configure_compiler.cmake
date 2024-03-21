message (STATUS "Setting up ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} compiler ...")

string (REPLACE "." ";" VERSION_LIST ${CMAKE_CXX_COMPILER_VERSION})
list (GET VERSION_LIST 0 COMPILER_VERSION_MAJOR)
list (GET VERSION_LIST 1 COMPILER_VERSION_MINOR)

if (CMAKE_COMPILER_IS_GNUCXX)
  # configure GNU C++ compiler

  set (COMPILER_NAME "gcc")
  set (COMPILER_VERSION ${COMPILER_VERSION_MAJOR})
  set (COMPILER_LIBCXX "libstdc++11")

  # format error messages so that they fit on lines of about 72 chars
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmessage-length=72")

  # enable colors in gcc log output.
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")

  # code coverage.
  set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fprofile-arcs -ftest-coverage")

  if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
      option (SUPPORT_STD_FORMAT ON)
  endif()
elseif ((CMAKE_CXX_COMPILER_ID MATCHES "Clang") OR (CMAKE_CXX_COMPILER_ID MATCHES "AppleClang"))
  # configure Clang C++ compiler

  if (CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
    set (COMPILER_NAME "apple-clang")
    set (COMPILER_VERSION ${COMPILER_VERSION_MAJOR}.${COMPILER_VERSION_MINOR})
  else()
    set (COMPILER_NAME "clang")
    set (COMPILER_VERSION ${COMPILER_VERSION_MAJOR})

    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
      option (SUPPORT_STD_FORMAT ON)
    endif()
  endif()

  set (COMPILER_LIBCXX "libc++")

  # enable colors in clang log output
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics")

  # set correct standard library
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=${COMPILER_LIBCXX}")
else()
  message (FATAL_ERROR "${CMAKE_CXX_COMPILER_ID} compiler not supported!")
endif()

# base compilation flags
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wextra")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wcast-align")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wpointer-arith")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunused-variable")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wnon-virtual-dtor")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-long-long")

# set the symbol visibility flag (this line makes "hidden" the default visibility for all symbols of a shared library)
set (CMAKE_CXX_VISIBILITY_PRESET hidden)
set (CMAKE_VISIBILITY_INLINES_HIDDEN 1)

# http://www.openbsd.org/papers/nycbsdcon08-pie/
# Just setting CMAKE_POSITION_INDEPENDENT_CODE should be enough to set -fPIC for GCC
# but sometimes it still doesn't get set, so make sure it does
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC") # Generate position-independent code

# specific flags for Release builds.
set (CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -Werror")
# specific flags for RelWithDebInfo.
set (CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2 -g -DNDEBUG -DWITH_DEBUG_INFO")
# specific flags for Debug builds.
set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -Og -DDEBUG -D_DEBUG -fno-inline")

if (ENABLE_ASAN)
  # Enable address sanitizer
  #   -ggdb an -fno-omit-frame-pointer: enable the call stack and line number for better report format to locate bug in user code
  #   -fsanitize=address: address level detection, heap and stack
  set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -ggdb -fno-omit-frame-pointer")
  if (CMAKE_COMPILER_IS_GNUCXX)
    set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -static-libasan")
  endif()
endif()

if (ENABLE_UBSAN)
  # Enable Undefined Behavior Sanitizer (UBSAN)
  #  -fno-sanitize-recover: abort when a ubsan error is found
  set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=undefined -fno-sanitize-recover=all")
endif ()

if (HA_ENABLE_TSAN)
  # Enable Thread Sanitizer
  set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=thread")
endif()

# defines
add_compile_definitions (_USE_MATH_DEFINES) # by default, use the math constants defined in <cmath> header

# enable C++20
if (NOT CMAKE_CXX_STANDARD)
  set (CMAKE_CXX_STANDARD 20)
endif ()
set (CMAKE_CXX_STANDARD_REQUIRED TRUE)