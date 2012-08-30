// RUN: %clang_cc1 -fsyntax-only -verify -Wparentheses -Wno-objc-root-class %s

// Don't warn about some common ObjC idioms unless we have -Widiomatic-parentheses on.
// <rdar://problem/7382435>

@interface Object 
- (id) init;
- (id) initWithInt: (int) i;
- (void) iterate: (id) coll;
- (id) nextObject;
@end

@implementation Object
- (id) init {
  if (self = [self init]) {
  }
  return self;
}

- (id) initWithInt: (int) i {
  if (self = [self initWithInt: i]) {
  }
  return self;
}

- (void) iterate: (id) coll {
  id cur;
  while (cur = [coll nextObject]) {
  }
}

- (id) nextObject {
  return self;
}
@end
