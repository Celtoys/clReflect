#! /bin/bash

mkdir -p llvm-build-gnu
cd llvm-build-gnu

tr -d '\r' < ../llvm/autoconf/config.guess > tmp.guess
cp tmp.guess ../llvm/autoconf/config.guess
rm tmp.guess

CC=/usr/bin/cc cmake -G "Unix Makefiles" ../llvm && make
