@echo off
pushd .
md llvm-build
cd llvm-build
cmake -G "Visual Studio 8 2005" ..\llvm
popd