@echo off
pushd .
svn co https://llvm.org/svn/llvm-project/llvm/branches/release_30/ llvm
cd llvm\tools
svn co https://llvm.org/svn/llvm-project/cfe/branches/release_30/ clang
popd