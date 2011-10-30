// RUN: %clang_cc1 -I%S %s -triple x86_64-apple-darwin10 -emit-llvm -fcxx-exceptions -fexceptions -std=c++11 -o - | FileCheck %s
struct A { virtual ~A(); };
struct B final : A { };
struct C { virtual ~C(); int c; };

// CHECK: @_Z1fP1B
C *f(B* b) {
  // CHECK-NOT: call i8* @__dynamic_cast
  // CHECK: ret %struct.C* null
  return dynamic_cast<C*>(b);
}

// CHECK: @_Z1fR1B
C &f(B& b) {
  // CHECK-NOT: call i8* @__dynamic_cast
  // CHECK: call void @__cxa_bad_cast() noreturn
  // CHECK: ret %struct.C* undef
  return dynamic_cast<C&>(b);
}
