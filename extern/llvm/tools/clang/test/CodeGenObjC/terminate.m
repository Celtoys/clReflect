// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-fragile-abi -emit-llvm -fexceptions -fobjc-exceptions -fobjc-runtime-has-terminate -o - %s | FileCheck %s -check-prefix=CHECK-WITH
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-fragile-abi -emit-llvm -fexceptions -fobjc-exceptions -o - %s | FileCheck %s -check-prefix=CHECK-WITHOUT

void destroy(void**);

// rdar://problem/9519113
void test0(void) {
  void test0_helper(void);
  void *ptr __attribute__((cleanup(destroy)));
  test0_helper();

  // CHECK-WITH:       define void @test0()
  // CHECK-WITH:         [[PTR:%.*]] = alloca i8*,
  // CHECK-WITH:         call void @destroy(i8** [[PTR]])
  // CHECK-WITH-NEXT:    ret void
  // CHECK-WITH:         invoke void @destroy(i8** [[PTR]])
  // CHECK-WITH:         landingpad { i8*, i32 } personality i8* bitcast (i32 (...)* @__gcc_personality_v0 to i8*)
  // CHECK-WITH-NEXT:      catch i8* null
  // CHECK-WITH-NEXT:    call void @objc_terminate()

  // CHECK-WITHOUT:    define void @test0()
  // CHECK-WITHOUT:      [[PTR:%.*]] = alloca i8*,
  // CHECK-WITHOUT:      call void @destroy(i8** [[PTR]])
  // CHECK-WITHOUT-NEXT: ret void
  // CHECK-WITHOUT:      invoke void @destroy(i8** [[PTR]])
  // CHECK-WITHOUT:      landingpad { i8*, i32 } personality i8* bitcast (i32 (...)* @__gcc_personality_v0 to i8*)
  // CHECK-WITHOUT-NEXT:   catch i8* null
  // CHECK-WITHOUT-NEXT: call void @abort()
}
