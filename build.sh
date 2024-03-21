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

if [[ $(uname -s) == "Darwin" ]]; then
  NUM_CORES=$(sysctl -n hw.physicalcpu)
elif [[ $(uname -s) == "Linux" ]]; then
  NUM_CORES=$(nproc)
else
  echo "ERROR: current system is not supported"

  exit 1
fi

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

  docker build -f docker/Dockerfile.$COMPILER --build-arg DOCKER_USER=$USER -t $DOCKER_IMAGE_NAME .

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
  mkdir -p $BUILD_DIR
fi

CMAKE_OPTIONS+="-DCMAKE_BUILD_TYPE=${BUILD_TYPE} "
CMAKE_OPTIONS+="-DCMAKE_MODULE_PATH=$PWD/$BUILD_DIR "

pushd $BUILD_DIR
  if [[ ! -f conaninfo.txt ]]; then
    # conan profile detect
    cmake -G "${GENERATOR}" ${CMAKE_OPTIONS} $ROOT_DIR
  fi

  cmake --build . --parallel $NUM_CORES --target all $([ $GEN_DOC -eq 1 ] && echo "documentation")

  if [[ $RUN_TESTS -eq 1 ]]; then
    GTEST_COLOR=yes ctest --verbose
  fi
popd
