// RUN: rm -rf %t
// RUN: mkdir %t
// RUN: echo '[{"directory":".","command":"clang++ -c %t/test.cpp","file":"%t/test.cpp"}]' > %t/compile_commands.json
// RUN: cp "%s" "%t/test.cpp"
// RUN: clang-check "%t" "%t/test.cpp" 2>&1|FileCheck %s
// FIXME: Make the above easier.

// CHECK: C++ requires
invalid;

// FIXME: JSON doesn't like path separator '\', on Win32 hosts.
// FIXME: clang-check doesn't like gcc driver on cygming.
// XFAIL: cygwin,mingw32,win32
