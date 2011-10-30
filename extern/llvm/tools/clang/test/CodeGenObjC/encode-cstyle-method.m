// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fobjc-fragile-abi  -emit-llvm -o - %s | FileCheck -check-prefix LP64 %s
// rdar: // 7445205

@interface Foo 
- (id)test:(id)one, id two;
@end

@implementation Foo
- (id)test:(id )one, id two {return two; } @end

// CHECK-LP64: internal global [11 x i8] c"@24@0:8@16
