// RUN: %clang_cc1 -fsyntax-only -verify %s
int foo(int);

namespace N {
  void f1() {
    void foo(int); // okay
  }

  // FIXME: we shouldn't even need this declaration to detect errors
  // below.
  void foo(int); // expected-note{{previous declaration is here}}

  void f2() {
    int foo(int); // expected-error{{functions that differ only in their return type cannot be overloaded}}

    {
      int foo;
      {
        // FIXME: should diagnose this because it's incompatible with
        // N::foo. However, name lookup isn't properly "skipping" the
        // "int foo" above.
        float foo(int); 
      }
    }
  }
}

class A {
 void typocorrection(); // expected-note {{'typocorrection' declared here}}
};

void A::Notypocorrection() { // expected-error {{out-of-line definition of 'Notypocorrection' does not match any declaration in 'A'; did you mean 'typocorrection'}}
}


namespace test0 {
  void dummy() {
    void Bar(); // expected-note {{'Bar' declared here}}
    class A {
      friend void bar(); // expected-error {{no matching function 'bar' found in local scope; did you mean 'Bar'}}
    };
  }
}


class B {
 void typocorrection(const int); // expected-note {{'typocorrection' declared here}}
 void typocorrection(double);
};

void B::Notypocorrection(int) { // expected-error {{out-of-line definition of 'Notypocorrection' does not match any declaration in 'B'; did you mean 'typocorrection'}}
}

struct X { int f(); };
struct Y : public X {};
int Y::f() { return 3; } // expected-error {{out-of-line definition of 'f' does not match any declaration in 'Y'}}

namespace test1 {
struct Foo {
  class Inner { };
};
}

class Bar {
  void f(test1::Foo::Inner foo) const; // expected-note {{member declaration does not match because it is const qualified}}
};

using test1::Foo;

void Bar::f(Foo::Inner foo) { // expected-error {{out-of-line definition of 'f' does not match any declaration in 'Bar'}}
  (void)foo;
}

class Crash {
 public:
  void GetCart(int count) const;
};
// This out-of-line definition was fine...
void Crash::cart(int count) const {} // expected-error {{out-of-line definition of 'cart' does not match any declaration in 'Crash'}} \
                                     // expected-note {{'cart' declared here}} \
                                     // expected-note {{previous definition is here}}
// ...while this one crashed clang
void Crash::chart(int count) const {} // expected-error {{out-of-line definition of 'chart' does not match any declaration in 'Crash'; did you mean 'cart'?}} \
                                      // expected-error {{redefinition of 'cart'}}

class TestConst {
 public:
  int getit() const; // expected-note {{member declaration does not match because it is const qualified}}
  void setit(int); // expected-note {{member declaration does not match because it is not const qualified}}
};

int TestConst::getit() { // expected-error {{out-of-line definition of 'getit' does not match any declaration in 'TestConst'}}
  return 1;
}

void TestConst::setit(int) const { // expected-error {{out-of-line definition of 'setit' does not match any declaration in 'TestConst'}}
}

struct J { int typo() const; };
int J::typo_() { return 3; } // expected-error {{out-of-line definition of 'typo_' does not match any declaration in 'J'}}
