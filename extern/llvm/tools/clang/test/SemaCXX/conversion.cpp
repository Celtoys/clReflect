// RUN: %clang_cc1 -triple x86_64-apple-darwin -fsyntax-only -Wconversion -verify %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin -fsyntax-only -Wconversion %s 2>&1 | FileCheck %s

#include <stddef.h>

typedef   signed char  int8_t;
typedef   signed short int16_t;
typedef   signed int   int32_t;
typedef   signed long  int64_t;

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uint64_t;

// <rdar://problem/7909130>
namespace test0 {
  int32_t test1_positive(char *I, char *E) {
    return (E - I); // expected-warning {{implicit conversion loses integer precision}}
  }

  int32_t test1_negative(char *I, char *E) {
    return static_cast<int32_t>(E - I);
  }

  uint32_t test2_positive(uint64_t x) {
    return x; // expected-warning {{implicit conversion loses integer precision}}
  }

  uint32_t test2_negative(uint64_t x) {
    return (uint32_t) x;
  }
}

namespace test1 {
  uint64_t test1(int x, unsigned y) {
    return sizeof(x == y);
  }

  uint64_t test2(int x, unsigned y) {
    return __alignof(x == y);
  }

  void * const foo();
  bool test2(void *p) {
    return p == foo();
  }
}

namespace test2 {
  struct A {
    unsigned int x : 2;
    A() : x(10) {} // expected-warning {{implicit truncation from 'int' to bitfield changes value from 10 to 2}}
  };
}

// This file tests -Wnull-conversion, a subcategory of -Wconversion
// which is on by default.

void test3() {
  int a = NULL; // expected-warning {{implicit conversion of NULL constant to 'int'}}
  int b;
  b = NULL; // expected-warning {{implicit conversion of NULL constant to 'int'}}
  long l = NULL; // FIXME: this should also warn, but currently does not if sizeof(NULL)==sizeof(inttype)
  int c = ((((NULL)))); // expected-warning {{implicit conversion of NULL constant to 'int'}}
  int d;
  d = ((((NULL)))); // expected-warning {{implicit conversion of NULL constant to 'int'}}
  bool bl = NULL; // FIXME: this should warn but we currently suppress a bunch of conversion-to-bool warnings including this one
  char ch = NULL; // expected-warning {{implicit conversion of NULL constant to 'char'}}
  unsigned char uch = NULL; // expected-warning {{implicit conversion of NULL constant to 'unsigned char'}}
  short sh = NULL; // expected-warning {{implicit conversion of NULL constant to 'short'}}

  // Use FileCheck to ensure we don't get any unnecessary macro-expansion notes 
  // (that don't appear as 'real' notes & can't be seen/tested by -verify)
  // CHECK-NOT: note:
  // CHECK: note: expanded from macro 'FNULL'
#define FNULL NULL
  int a2 = FNULL; // expected-warning {{implicit conversion of NULL constant to 'int'}}
  // CHECK-NOT: note:
  // CHECK: note: expanded from macro 'FINIT'
#define FINIT int a3 = NULL;
  FINIT // expected-warning {{implicit conversion of NULL constant to 'int'}}
}
