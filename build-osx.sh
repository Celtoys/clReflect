#! /bin/bash

pushd .
mkdir cmake-build
cd cmake-build
mkdir osx
cd osx
CC=/usr/bin/cc cmake -G "Unix Makefiles" ../..
popd
