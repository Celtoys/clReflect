@echo off
pushd .
md llvm-build
cd llvm-build
cmake -G "Visual Studio 9 2008" ..\llvm
popd