
pushd .
md llvm-build
cd llvm-build
cmake %* -G "Visual Studio 10" ..\llvm
popd