// RUN: %clang_cc1 -fsyntax-only -verify %s

typedef __typeof((int*) 0 - (int*) 0) intptr_t;

static int f = 10;
static int b = f; // expected-error {{initializer element is not a compile-time constant}}

float r  = (float) (intptr_t) &r; // expected-error {{initializer element is not a compile-time constant}}
intptr_t s = (intptr_t) &s;
_Bool t = &t;


union bar {
  int i;
};

struct foo {
  unsigned ptr;
};

union bar u[1];
struct foo x = {(intptr_t) u}; // no-error
struct foo y = {(char) u}; // expected-error {{initializer element is not a compile-time constant}}
