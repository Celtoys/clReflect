// RUN: %clang_cc1 -fsyntax-only -verify %s

// -- prvalue of arithmetic

bool b = !0;

bool b2 = !1.2;

bool b3 = !4;

// -- unscoped enumeration
enum { E, F };

bool b4 = !E;
bool b5 = !F;

// --  pointer, 
bool b6 = !&b4;
void f();
bool b61 = !&f;

// -- or pointer to member type can be converted to a prvalue of type bool.
struct S { void f() { } };

bool b7 = !&S::f;


bool b8 = !S(); //expected-error {{invalid argument type 'S'}}

namespace PR8181
{
  bool f() { } // expected-note{{possible target for call}}
  void f(char) { } // expected-note{{possible target for call}}
  bool b = !&f;  //expected-error {{reference to overloaded function could not be resolved; did you mean to call it with no arguments?}}
}
