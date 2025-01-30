#!/usr/bin/env bash

# name of build directory
BUILD_DIR="build"

# remove build directory if it exists
if [ -d "$BUILD_DIR" ]; then rm -rf $BUILD_DIR; fi

# optionally set compiler
# export CC=clang
# export CXX=clang++

# set build mode
export CMAKE_BUILD_TYPE=RelWithDebInfo

# settings for debugging
# export USE_ASAN=ON # enables address sanitizer
# export USE_USAN=ON # enables undefined behavior sanitizer
# export USE_PG=ON # enables GNU profiler

# make build dir
mkdir $BUILD_DIR

# run cmake
PROJECT_ROOT=$(pwd)
(cd $BUILD_DIR && cmake $PROJECT_ROOT)

# run make
(cd $BUILD_DIR && make -j)
