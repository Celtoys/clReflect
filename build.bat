@echo off
pushd .
md cmake-build
cd cmake-build
md msvc
cd msvc
cmake -G "Visual Studio 8 2005" ..\..
popd
