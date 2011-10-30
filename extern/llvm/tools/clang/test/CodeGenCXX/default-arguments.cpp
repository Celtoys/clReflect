// RUN: %clang_cc1 %s -triple=x86_64-apple-darwin10 -emit-llvm -o - | FileCheck %s

// PR5484
namespace PR5484 {
struct A { };
extern A a;

void f(const A & = a);

void g() {
  f();
}
}

struct A1 {
 A1();
 ~A1();
};

struct A2 {
 A2();
 ~A2();
};

struct B {
 B(const A1& = A1(), const A2& = A2());
};

// CHECK: define void @_Z2f1v()
void f1() {

 // CHECK: call void @_ZN2A1C1Ev(
 // CHECK: call void @_ZN2A2C1Ev(
 // CHECK: call void @_ZN1BC1ERK2A1RK2A2(
 // CHECK: call void @_ZN2A2D1Ev
 // CHECK: call void @_ZN2A1D1Ev
 B bs[2];
}

struct C {
 B bs[2];
 C();
};

// CHECK: define void @_ZN1CC1Ev(%struct.C* %this) unnamed_addr
// CHECK: call void @_ZN1CC2Ev(

// CHECK: define void @_ZN1CC2Ev(%struct.C* %this) unnamed_addr
// CHECK: call void @_ZN2A1C1Ev(
// CHECK: call void @_ZN2A2C1Ev(
// CHECK: call void @_ZN1BC1ERK2A1RK2A2(
// CHECK: call void @_ZN2A2D1Ev
// CHECK: call void @_ZN2A1D1Ev
C::C() { }

// CHECK: define void @_Z2f3v()
void f3() {
 // CHECK: call void @_ZN2A1C1Ev(
 // CHECK: call void @_ZN2A2C1Ev(
 // CHECK: call void @_ZN1BC1ERK2A1RK2A2(
 // CHECK: call void @_ZN2A2D1Ev
 // CHECK: call void @_ZN2A1D1Ev
 B *bs = new B[2];
 delete bs;
}
