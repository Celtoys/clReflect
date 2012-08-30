// RUN: %clang_cc1 -pedantic-errors -std=c++11 -emit-pch %s -o %t
// RUN: %clang_cc1 -pedantic-errors -std=c++11 -include-pch %t -verify %s

#ifndef HEADER_INCLUDED

#define HEADER_INCLUDED

struct B {
  B(); // expected-note {{here}}
  constexpr B(char) {}
};

struct C { // expected-note {{not an aggregate and has no constexpr constructors}}
  B b;
  double d = 0.0;
};

struct D : B {
  constexpr D(int n) : B('x'), k(2*n+1) {}
  int k;
};

#else

static_assert(D(4).k == 9, "");
constexpr int f(C c) { return 0; } // expected-error {{not a literal type}}
constexpr B b; // expected-error {{constant expression}} expected-note {{non-constexpr}}

#endif
