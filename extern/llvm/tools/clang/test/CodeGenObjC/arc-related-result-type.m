// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fblocks -fobjc-arc -o - %s | FileCheck %s

@interface Test0
- (id) self;
@end
void test0(Test0 *val) {
  Test0 *x = [val self];

// CHECK:    define void @test0(
// CHECK:      [[VAL:%.*]] = alloca [[TEST0:%.*]]*
// CHECK-NEXT: [[X:%.*]] = alloca [[TEST0]]*
// CHECK-NEXT: bitcast
// CHECK-NEXT: call i8* @objc_retain(
// CHECK-NEXT: bitcast
// CHECK-NEXT: store
// CHECK-NEXT: load [[TEST0]]** [[VAL]],
// CHECK-NEXT: load
// CHECK-NEXT: bitcast
// CHECK-NEXT: [[T0:%.*]] = call i8* bitcast (
// CHECK-NEXT: [[T1:%.*]] = call i8* @objc_retainAutoreleasedReturnValue(i8* [[T0]])
// CHECK-NEXT: [[T2:%.*]] = bitcast i8* [[T1]] to [[TEST0]]*
// CHECK-NEXT: store [[TEST0]]* [[T2]], [[TEST0]]** [[X]]
// CHECK-NEXT: [[T0:%.*]] = load [[TEST0]]** [[X]]
// CHECK-NEXT: [[T1:%.*]] = bitcast [[TEST0]]* [[T0]] to i8*
// CHECK-NEXT: call void @objc_release(i8* [[T1]])
// CHECK-NEXT: [[T0:%.*]] = load [[TEST0]]** [[VAL]]
// CHECK-NEXT: [[T1:%.*]] = bitcast [[TEST0]]* [[T0]] to i8*
// CHECK-NEXT: call void @objc_release(i8* [[T1]])
// CHECK-NEXT: ret void
}
