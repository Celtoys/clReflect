// RUN: %clang_cc1 -triple i386-apple-darwin9 -fobjc-fragile-abi -emit-llvm -fblocks -o - %s | FileCheck %s

// test1.  All of this is somehow testing rdar://6676764
struct S {
  void (^F)(struct S*);
} P;


@interface T
  - (int)foo: (T (^)(T*)) x;
@end

void foo(T *P) {
 [P foo: 0];
}

@interface A 
-(void) im0;
@end

// CHECK: define internal i32 @"__8-[A im0]_block_invoke_0"(
@implementation A
-(void) im0 {
  (void) ^{ return 1; }();
}
@end

@interface B : A @end
@implementation B
-(void) im1 {
  ^(void) { [self im0]; }();
}
-(void) im2 {
  ^{ [super im0]; }();
}
-(void) im3 {
  ^{ ^{[super im0];}(); }();
}
@end

// rdar://problem/9006315
// In-depth test for the initialization of a __weak __block variable.
@interface Test2 -(void) destroy; @end
void test2(Test2 *x) {
  extern void test2_helper(void (^)(void));
  // CHECK:    define void @test2(
  // CHECK:      [[X:%.*]] = alloca [[TEST2:%.*]]*,
  // CHECK-NEXT: [[WEAKX:%.*]] = alloca [[WEAK_T:%.*]],
  // CHECK-NEXT: [[BLOCK:%.*]] = alloca [[BLOCK_T:<{.*}>]],
  // CHECK-NEXT: store [[TEST2]]*

  // isa=1 for weak byrefs.
  // CHECK-NEXT: [[T0:%.*]] = getelementptr inbounds [[WEAK_T]]* [[WEAKX]], i32 0, i32 0
  // CHECK-NEXT: store i8* inttoptr (i32 1 to i8*), i8** [[T0]]

  // Forwarding.
  // CHECK-NEXT: [[T1:%.*]] = getelementptr inbounds [[WEAK_T]]* [[WEAKX]], i32 0, i32 1
  // CHECK-NEXT: store [[WEAK_T]]* [[WEAKX]], [[WEAK_T]]** [[T1]]

  // Flags.  This is just BLOCK_HAS_COPY_DISPOSE.
  // CHECK-NEXT: [[T2:%.*]] = getelementptr inbounds [[WEAK_T]]* [[WEAKX]], i32 0, i32 2
  // CHECK-NEXT: store i32 33554432, i32* [[T2]]

  // Size.
  // CHECK-NEXT: [[T3:%.*]] = getelementptr inbounds [[WEAK_T]]* [[WEAKX]], i32 0, i32 3
  // CHECK-NEXT: store i32 28, i32* [[T3]]

  // Copy and dipose helpers.
  // CHECK-NEXT: [[T4:%.*]] = getelementptr inbounds [[WEAK_T]]* [[WEAKX]], i32 0, i32 4
  // CHECK-NEXT: store i8* bitcast (void (i8*, i8*)* @__Block_byref_object_copy_{{.*}} to i8*), i8** [[T4]]
  // CHECK-NEXT: [[T5:%.*]] = getelementptr inbounds [[WEAK_T]]* [[WEAKX]], i32 0, i32 5
  // CHECK-NEXT: store i8* bitcast (void (i8*)* @__Block_byref_object_dispose_{{.*}} to i8*), i8** [[T5]]

  // Actually capture the value.
  // CHECK-NEXT: [[T6:%.*]] = getelementptr inbounds [[WEAK_T]]* [[WEAKX]], i32 0, i32 6
  // CHECK-NEXT: [[CAPTURE:%.*]] = load [[TEST2]]** [[X]]
  // CHECK-NEXT: store [[TEST2]]* [[CAPTURE]], [[TEST2]]** [[T6]]

  // Then we initialize the block, blah blah blah.
  // CHECK:      call void @test2_helper(

  // Finally, kill the variable with BLOCK_FIELD_IS_BYREF.  We're not
  // supposed to pass BLOCK_FIELD_IS_WEAK here.
  // CHECK:      [[T0:%.*]] = bitcast [[WEAK_T]]* [[WEAKX]] to i8*
  // CHECK:      call void @_Block_object_dispose(i8* [[T0]], i32 8)

  __weak __block Test2 *weakX = x;
  test2_helper(^{ [weakX destroy]; });
}

// rdar://problem/9124263
// In the test above, check that the use in the invocation function
// doesn't require a read barrier.
// CHECK:    define internal void @__test2_block_invoke_
// CHECK:      [[BLOCK:%.*]] = bitcast i8* {{%.*}} to [[BLOCK_T]]*
// CHECK-NEXT: [[T0:%.*]] = getelementptr inbounds [[BLOCK_T]]* [[BLOCK]], i32 0, i32 5
// CHECK-NEXT: [[T1:%.*]] = load i8** [[T0]]
// CHECK-NEXT: [[T2:%.*]] = bitcast i8* [[T1]] to [[WEAK_T]]{{.*}}*
// CHECK-NEXT: [[T3:%.*]] = getelementptr inbounds [[WEAK_T]]{{.*}}* [[T2]], i32 0, i32 1
// CHECK-NEXT: [[T4:%.*]] = load [[WEAK_T]]{{.*}}** [[T3]]
// CHECK-NEXT: [[WEAKX:%.*]] = getelementptr inbounds [[WEAK_T]]{{.*}}* [[T4]], i32 0, i32 6
// CHECK-NEXT: [[T0:%.*]] = load [[TEST2]]** [[WEAKX]], align 4
