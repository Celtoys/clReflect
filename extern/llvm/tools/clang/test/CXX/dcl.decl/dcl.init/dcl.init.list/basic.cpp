// RUN: %clang_cc1 -fsyntax-only -verify %s

void f0() {
  int &ir = { 17 }; // expected-error{{reference to type 'int' cannot bind to an initializer list}}
}
