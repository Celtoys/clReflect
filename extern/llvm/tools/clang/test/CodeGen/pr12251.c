// RUN: %clang_cc1 %s -emit-llvm -O1 -relaxed-aliasing -o - | FileCheck %s

enum e1 {e1_a = -1 };
enum e1 g1(enum e1 *x) {
  return *x;
}

// CHECK: define i32 @g1
// CHECK: load i32* %x, align 4
// CHECK-NOT: range
// CHECK: ret
