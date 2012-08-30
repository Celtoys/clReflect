// RUN: %clang_cc1 -analyze -analyzer-checker=core,experimental.core -analyzer-constraints=basic -analyzer-store=region -verify -Wno-objc-root-class %s

typedef struct Foo { int x; } Bar;

@interface MyClass {}
- (Bar)foo;
@end
@implementation MyClass
- (Bar)foo { 
  struct Foo f = { 0 };
  return f;
}
@end

void createFoo() {
  MyClass *obj = 0;  
  Bar f = [obj foo]; // no-warning
}

void createFoo2() {
  MyClass *obj = 0;  
  [obj foo]; // no-warning
  Bar f = [obj foo]; // no-warning
}

