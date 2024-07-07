#!/bin/bash
set -e

ROOT_DIR=$(pwd)
BUILD_TYPE=Debug
GENERATOR="Unix Makefiles"
CMAKE_OPTIONS=""
RUN_TESTS=0
RUN_DOCKER=0
GEN_DOC=0
COMPILER=gcc13
CLEAN=0
OPEN_IDE=0
GENERATE_PROJECT=0

if [[ $(uname -s) == "Darwin" ]]; then
  NUM_CORES=$(sysctl -n hw.physicalcpu)
elif [[ $(uname -s) == "Linux" ]]; then
  NUM_CORES=$(nproc)
else
  echo "ERROR: current system is not supported"

  exit 1
fi

help () {
  echo "Usage: $1 [options]"
  echo "Options:"
  echo "  clean           remove build directory"
  echo "  release         build in release mode"
  echo "  verbose         enable verbose mode for makefile generator"
  echo "  ninja           use ninja generator"
  echo "  mold            use mold linker"
  echo "  asan=on         enable address sanitizer"
  echo "  ubsan=on        enable undefined behavior sanitizer"
  echo "  test            run tests"
  echo "  docker          run build in docker with gcc13"
  echo "  docker=gcc13    run build in docker with gcc13"
  echo "  docker=clang17  run build in docker with clang17"
  echo "  cxx14           set C++14 standard"
  echo "  cxx17           set C++17 standard"
  echo "  cxx23           set C++23 standard"
  echo "  doc             generate documentation"
  echo "  gen|generate    generate project"
  echo "  open            open IDE (for xcode only)"
  echo "  xcode           generate Xcode project for macOS arm64"
  echo "  xcode=ios       generate Xcode project for iOS"
  echo "  xcode=sim       generate Xcode project for iOS simulator arm64"
  echo "  help            show this message"

  exit 0
}

for I in "$@"; do
  if [[ $I == "clean" ]]; then
    CLEAN=1
  fi

  if [[ $I == "release" ]]; then
    BUILD_TYPE=Release
  fi

  if [[ $I == "verbose" ]]; then
    CMAKE_OPTIONS+="-DCMAKE_VERBOSE_MAKEFILE:BOOL=ON "
  fi

  if [[ $I == "ninja" ]]; then
    GENERATOR=Ninja
  fi

  if [[ $I == "mold" && -x "$(command -v mold)" ]]; then
    CMAKE_OPTIONS+="-DCMAKE_EXE_LINKER_FLAGS_INIT=-B/usr/local/libexec/mold  "
    CMAKE_OPTIONS+="-DCMAKE_SHARED_LINKER_FLAGS_INIT=-B/usr/local/libexec/mold "
    CMAKE_OPTIONS+="-DCMAKE_MODULE_LINKER_FLAGS_INIT=-B/usr/local/libexec/mold "
  fi

  if [[ $I == "asan=on" ]]; then
    CMAKE_OPTIONS+="-DENABLE_ASAN:BOOL=ON "
  fi

  if [[ $I == "ubsan=on" ]]; then
    CMAKE_OPTIONS+="-DENABLE_UBSAN:BOOL=ON "
  fi

  if [[ $I == "test" ]]; then
    RUN_TESTS=1
  fi

  if [[ $I =~ ^docker$|^docker=.*  ]]; then
    RUN_DOCKER=1
    DOCKER_VALUE=${I:7:15}

    if [[ $DOCKER_VALUE != "" ]]; then
      COMPILER=$DOCKER_VALUE
    fi
  fi

  if [[ $I =~ ^cxx[0-9]{2}$ ]]; then
    CMAKE_OPTIONS+="-DCMAKE_CXX_STANDARD=${I:3:5} "
  fi

  if [[ $I == "doc" ]]; then
    GEN_DOC=1
  fi

  if [[ $I == "gen" || $I == "generate" ]]; then
    GENERATE_PROJECT=1
  fi

  if [[ $I == "open" ]]; then
    OPEN_IDE=1
  fi

  if [[ $I =~ ^xcode$|xcode=.* ]]; then
    GENERATOR=Xcode

    APPLE_PLATFORM=${I:6:20}

    CMAKE_OPTIONS+="-DCMAKE_TOOLCHAIN_FILE=$ROOT_DIR/cmake/ios.toolchain.cmake "

    if [[ $APPLE_PLATFORM == "ios" ]]; then
      CMAKE_OPTIONS+="-DIOS=ON  -DDEPLOYMENT_TARGET=12.0 -DPLATFORM=OS64 "
    elif [[ $APPLE_PLATFORM == "sim" ]]; then
      CMAKE_OPTIONS+="-DIOS=ON  -DDEPLOYMENT_TARGET=12.0 -DPLATFORM=SIMULATORARM64 "
    else
      CMAKE_OPTIONS+="-DDEPLOYMENT_TARGET=13.3 -DPLATFORM=MAC_ARM64 "
    fi
  fi

  if [[ $I == "help" ]]; then
    help $0
  fi
done

if [[ -z $DEFAULT_BUILD_DIR ]]; then DEFAULT_BUILD_DIR=build; fi

BUILD_DIR=$DEFAULT_BUILD_DIR/$(echo $BUILD_TYPE | tr '[:upper:]' '[:lower:]')

if [[ $CLEAN -eq 1 ]]; then
  rm -rf $BUILD_DIR
fi

if [[ $RUN_DOCKER -eq 1 ]]; then
  if [[ ! -d .conan.$COMPILER ]]; then
    mkdir -p .conan.$COMPILER
  fi

  if [[ ! -d .ccache.$COMPILER ]]; then
    mkdir -p .ccache.$COMPILER
  fi

  DOCKER_IMAGE_NAME=linux-ubuntu-cxx:$COMPILER

  if [[ $JUST_COMPILE -eq 0 ]]; then
    docker build -f docker/Dockerfile.$COMPILER --build-arg DOCKER_USER=$USER -t $DOCKER_IMAGE_NAME .
  fi

  ARGS=$(echo $@ | sed 's/docker=[a-z0-9]*//' | sed 's/docker//g' | sed 's/^[[:space:]]*//g')
  if [[ $ARGS != "" ]]; then
    DOCKER_RUN_CMD="$0 $ARGS"
  else
    DOCKER_RUN_CMD=/bin/bash
    DOCKER_TTY="t"
  fi

  docker run -i$DOCKER_TTY --rm \
    -v $PWD:/workspace/source \
    -v $PWD/.conan.$COMPILER/:/home/$USER/.conan \
    -v $PWD/.ccache.$COMPILER:/.ccache \
    -e ASAN_OPTIONS=$ASAN_OPTIONS \
    -w /workspace/source \
    --name $COMPILER \
    $DOCKER_IMAGE_NAME \
    $DOCKER_RUN_CMD

  exit 0
fi

if [[ ! -d $BUILD_DIR ]]; then
  GENERATE_PROJECT=1
  mkdir -p $BUILD_DIR
fi

CMAKE_OPTIONS+="-DCMAKE_BUILD_TYPE=${BUILD_TYPE} "
CMAKE_OPTIONS+="-DCMAKE_MODULE_PATH=$PWD/$BUILD_DIR "

pushd $BUILD_DIR
  if [[ ! -f conaninfo.txt || $GENERATE_PROJECT -eq 1 ]]; then
    # conan profile detect
    cmake -G "${GENERATOR}" ${CMAKE_OPTIONS} $ROOT_DIR
  fi

  if [[ $OPEN_IDE -eq 1 && $GENERATOR == "Xcode" ]]; then
    cmake --open .
    exit 0
  fi

  if [[ $GENERATOR != "Xcode" ]]; then
    CMAKE_TARGET="--target all"
  fi

  cmake --build . --parallel $NUM_CORES $CMAKE_TARGET $([ $GEN_DOC -eq 1 ] && echo "documentation")

  if [[ $RUN_TESTS -eq 1 ]]; then
    GTEST_COLOR=yes ctest --verbose -C $BUILD_TYPE
  fi
popd
