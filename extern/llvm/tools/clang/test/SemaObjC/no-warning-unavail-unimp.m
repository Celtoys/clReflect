// RUN: %clang_cc1  -fsyntax-only -verify %s
// rdar://9651605

@interface Foo
@property (getter=getVal) int val __attribute__((unavailable));
- Method __attribute__((unavailable));
+ CMethod __attribute__((unavailable));
@end

@implementation Foo
@end

