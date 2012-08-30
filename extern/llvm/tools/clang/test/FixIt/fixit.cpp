// RUN: %clang_cc1 -pedantic -Wall -Wno-comment -verify -fcxx-exceptions -x c++ %s
// RUN: cp %s %t
// RUN: not %clang_cc1 -pedantic -Wall -Wno-comment -fcxx-exceptions -fixit -x c++ %t
// RUN: %clang_cc1 -fsyntax-only -pedantic -Wall -Werror -Wno-comment -fcxx-exceptions -x c++ %t

/* This is a test of the various code modification hints that are
   provided as part of warning or extension diagnostics. All of the
   warnings will be fixed by -fixit, and the resulting file should
   compile cleanly with -Werror -pedantic. */

struct C1 {
  virtual void f();
  static void g();
};
struct C2 : virtual public virtual C1 { }; // expected-error{{duplicate}}

virtual void C1::f() { } // expected-error{{'virtual' can only be specified inside the class definition}}

static void C1::g() { } // expected-error{{'static' can only be specified inside the class definition}}

template<int Value> struct CT { }; // expected-note{{previous use is here}}

CT<10 >> 2> ct; // expected-warning{{require parentheses}}

class C3 {
public:
  C3(C3, int i = 0); // expected-error{{copy constructor must pass its first argument by reference}}
};

struct CT<0> { }; // expected-error{{'template<>'}}

template<> union CT<1> { }; // expected-error{{tag type}}

// Access declarations
class A {
protected:
  int foo();
};

class B : public A {
  A::foo; // expected-warning{{access declarations are deprecated}}
};

void f() throw(); // expected-note{{previous}}
void f(); // expected-warning{{missing exception specification}}

namespace rdar7853795 {
  struct A {
    bool getNumComponents() const; // expected-note{{declared here}}
    void dump() const {
      getNumComponenets(); // expected-error{{use of undeclared identifier 'getNumComponenets'; did you mean 'getNumComponents'?}}
    }
  };
}

namespace rdar7796492 {
  class A { int x, y; A(); };

  A::A()
    : x(1) y(2) { // expected-error{{missing ',' between base or member initializers}}
  }

}

// extra qualification on member
class C {
  int C::foo(); // expected-warning {{extra qualification}}
};

namespace rdar8488464 {
int x = 0;
int x1 &= 0; // expected-error {{invalid '&=' at end of declaration; did you mean '='?}}
int x2 *= 0; // expected-error {{invalid '*=' at end of declaration; did you mean '='?}}
int x3 += 0; // expected-error {{invalid '+=' at end of declaration; did you mean '='?}}
int x4 -= 0; // expected-error {{invalid '-=' at end of declaration; did you mean '='?}}
int x5 != 0; // expected-error {{invalid '!=' at end of declaration; did you mean '='?}}
int x6 /= 0; // expected-error {{invalid '/=' at end of declaration; did you mean '='?}}
int x7 %= 0; // expected-error {{invalid '%=' at end of declaration; did you mean '='?}}
int x8 <= 0; // expected-error {{invalid '<=' at end of declaration; did you mean '='?}}
int x9 <<= 0; // expected-error {{invalid '<<=' at end of declaration; did you mean '='?}}
int x10 >= 0; // expected-error {{invalid '>=' at end of declaration; did you mean '='?}}
int x11 >>= 0; // expected-error {{invalid '>>=' at end of declaration; did you mean '='?}}
int x12 ^= 0; // expected-error {{invalid '^=' at end of declaration; did you mean '='?}}
int x13 |= 0; // expected-error {{invalid '|=' at end of declaration; did you mean '='?}}
int x14 == 0; // expected-error {{invalid '==' at end of declaration; did you mean '='?}}

void f() {
    int x = 0;
    (void)x;
    int x1 &= 0; // expected-error {{invalid '&=' at end of declaration; did you mean '='?}}
    (void)x1;
    int x2 *= 0; // expected-error {{invalid '*=' at end of declaration; did you mean '='?}}
    (void)x2;
    int x3 += 0; // expected-error {{invalid '+=' at end of declaration; did you mean '='?}}
    (void)x3;
    int x4 -= 0; // expected-error {{invalid '-=' at end of declaration; did you mean '='?}}
    (void)x4;
    int x5 != 0; // expected-error {{invalid '!=' at end of declaration; did you mean '='?}}
    (void)x5;
    int x6 /= 0; // expected-error {{invalid '/=' at end of declaration; did you mean '='?}}
    (void)x6;
    int x7 %= 0; // expected-error {{invalid '%=' at end of declaration; did you mean '='?}}
    (void)x7;
    int x8 <= 0; // expected-error {{invalid '<=' at end of declaration; did you mean '='?}}
    (void)x8;
    int x9 <<= 0; // expected-error {{invalid '<<=' at end of declaration; did you mean '='?}}
    (void)x9;
    int x10 >= 0; // expected-error {{invalid '>=' at end of declaration; did you mean '='?}}
    (void)x10;
    int x11 >>= 0; // expected-error {{invalid '>>=' at end of declaration; did you mean '='?}}
    (void)x11;
    int x12 ^= 0; // expected-error {{invalid '^=' at end of declaration; did you mean '='?}}
    (void)x12;
    int x13 |= 0; // expected-error {{invalid '|=' at end of declaration; did you mean '='?}}
    (void)x13;
    int x14 == 0; // expected-error {{invalid '==' at end of declaration; did you mean '='?}}
    (void)x14;
    if (int x = 0)  { (void)x; }
    if (int x1 &= 0) { (void)x1; } // expected-error {{invalid '&=' at end of declaration; did you mean '='?}}
    if (int x2 *= 0) { (void)x2; } // expected-error {{invalid '*=' at end of declaration; did you mean '='?}}
    if (int x3 += 0) { (void)x3; } // expected-error {{invalid '+=' at end of declaration; did you mean '='?}}
    if (int x4 -= 0) { (void)x4; } // expected-error {{invalid '-=' at end of declaration; did you mean '='?}}
    if (int x5 != 0) { (void)x5; } // expected-error {{invalid '!=' at end of declaration; did you mean '='?}}
    if (int x6 /= 0) { (void)x6; } // expected-error {{invalid '/=' at end of declaration; did you mean '='?}}
    if (int x7 %= 0) { (void)x7; } // expected-error {{invalid '%=' at end of declaration; did you mean '='?}}
    if (int x8 <= 0) { (void)x8; } // expected-error {{invalid '<=' at end of declaration; did you mean '='?}}
    if (int x9 <<= 0) { (void)x9; } // expected-error {{invalid '<<=' at end of declaration; did you mean '='?}}
    if (int x10 >= 0) { (void)x10; } // expected-error {{invalid '>=' at end of declaration; did you mean '='?}}
    if (int x11 >>= 0) { (void)x11; } // expected-error {{invalid '>>=' at end of declaration; did you mean '='?}}
    if (int x12 ^= 0) { (void)x12; } // expected-error {{invalid '^=' at end of declaration; did you mean '='?}}
    if (int x13 |= 0) { (void)x13; } // expected-error {{invalid '|=' at end of declaration; did you mean '='?}}
    if (int x14 == 0) { (void)x14; } // expected-error {{invalid '==' at end of declaration; did you mean '='?}}
}
}

template <class A>
class F1 {
public:
  template <int B>
  class Iterator {
  };
};
 
template<class T>
class F2  {
  typename F1<T>:: /*template*/  Iterator<0> Mypos; // expected-error {{use 'template' keyword to treat 'Iterator' as a dependent template name}}
};

template <class T>
void f(){
  typename F1<T>:: /*template*/ Iterator<0> Mypos; // expected-error {{use 'template' keyword to treat 'Iterator' as a dependent template name}}
}

// Tests for &/* fixits radar 7113438.
class AD {};
class BD: public AD {};

void test (BD &br) {
  AD* aPtr;
  BD b;
  aPtr = b; // expected-error {{assigning to 'AD *' from incompatible type 'BD'; take the address with &}}
  aPtr = br; // expected-error {{assigning to 'AD *' from incompatible type 'BD'; take the address with &}}
}

void foo1() const {} // expected-error {{non-member function cannot have 'const' qualifier}}
void foo2() volatile {} // expected-error {{non-member function cannot have 'volatile' qualifier}}
void foo3() const volatile {} // expected-error {{non-member function cannot have 'const volatile' qualifier}}

struct S { void f(int, char); };
int itsAComma,
itsAComma2 = 0,
oopsAComma(42), // expected-error {{expected ';' at end of declaration}}
AD oopsMoreCommas() {
  static int n = 0, // expected-error {{expected ';' at end of declaration}}
  static char c,
  &d = c, // expected-error {{expected ';' at end of declaration}}
  S s, // expected-error {{expected ';' at end of declaration}}
  s.f(n, d);
  AD ad, // expected-error {{expected ';' at end of declaration}}
  return ad;
}
struct MoreAccidentalCommas {
  int a : 5,
      b : 7,
        : 4, // expected-error {{expected ';' at end of declaration}}
  char c, // expected-error {{expected ';' at end of declaration}}
  double d, // expected-error {{expected ';' at end of declaration}}
  MoreAccidentalCommas *next, // expected-error {{expected ';' at end of declaration}}
public:
  int k, // expected-error {{expected ';' at end of declaration}}
  friend void f(MoreAccidentalCommas) {}
  int k2, // expected-error {{expected ';' at end of declaration}}
  virtual void g(), // expected-error {{expected ';' at end of declaration}}
};

template<class T> struct Mystery;
template<class T> typedef Mystery<T>::type getMysteriousThing() { // \
  expected-error {{function definition declared 'typedef'}} \
  expected-error {{missing 'typename' prior to dependent}}
  return Mystery<T>::get();
}

template<template<typename> Foo, // expected-error {{template template parameter requires 'class' after the parameter list}}
         template<typename> typename Bar, // expected-error {{template template parameter requires 'class' after the parameter list}}
         template<typename> struct Baz> // expected-error {{template template parameter requires 'class' after the parameter list}}
void func();
