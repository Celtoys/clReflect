#! /bin/bash

mkdir -p llvm-build-gnu
cd llvm-build-gnu

tr -d '\r' < ../llvm/autoconf/config.guess > tmp.guess
cp tmp.guess ../llvm/autoconf/config.guess
rm tmp.guess

cmake -G "Unix Makefiles" ../llvm && make
