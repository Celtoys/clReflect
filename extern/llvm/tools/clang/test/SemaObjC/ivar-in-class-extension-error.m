// RUN: %clang_cc1 -fobjc-fragile-abi -fsyntax-only -verify %s
// rdar://6812436

@interface A @end

@interface A () { 
  int _p0; // expected-error {{ivars may not be placed in class extension}}
}
@property int p0;
@end

@interface A(CAT) { 
  int _p1; // expected-error {{ivars may not be placed in categories}}
}
@end
