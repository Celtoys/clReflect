
pushd .
mkdir llvm-build
cd llvm-build
cmake -G "Visual Studio 15 2017" -DLLVM_ENABLE_PROJECTS="clang" ..\llvm\llvm
popd