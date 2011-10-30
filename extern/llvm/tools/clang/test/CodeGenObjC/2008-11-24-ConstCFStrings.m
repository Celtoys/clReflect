// RUN: %clang_cc1 -emit-llvm -triple x86_64-apple-darwin -fobjc-fragile-abi %s -o - | FileCheck %s

// CHECK: _unnamed_cfstring_

@class NSString;

@interface A
- (void)bork:(NSString*)msg;
@end

void func(A *a) {
  [a bork:@"Hello world!"];
}
