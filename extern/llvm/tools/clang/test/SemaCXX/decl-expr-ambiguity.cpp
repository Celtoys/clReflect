// RUN: %clang_cc1 -fsyntax-only -verify -pedantic-errors %s

void f() {
  int a;
  struct S { int m; };
  typedef S *T;

  // Expressions.
  T(a)->m = 7;
  int(a)++; // expected-error {{assignment to cast is illegal}}
  __extension__ int(a)++; // expected-error {{assignment to cast is illegal}}
  __typeof(int)(a,5)<<a; // expected-error {{excess elements in scalar initializer}}
  void(a), ++a;
  if (int(a)+1) {}
  for (int(a)+1;;) {} // expected-warning {{expression result unused}}
  a = sizeof(int()+1);
  a = sizeof(int(1));
  typeof(int()+1) a2; // expected-error {{extension used}}
  (int(1)); // expected-warning {{expression result unused}}

  // type-id
  (int())1; // expected-error {{C-style cast from 'int' to 'int ()' is not allowed}}

  // Declarations.
  int fd(T(a)); // expected-warning {{parentheses were disambiguated as a function declarator}}
  T(*d)(int(p)); // expected-warning {{parentheses were disambiguated as a function declarator}} expected-note {{previous definition is here}}
  typedef T(*td)(int(p));
  extern T(*tp)(int(p));
  T d3(); // expected-warning {{empty parentheses interpreted as a function declaration}} expected-note {{replace parentheses with an initializer}}
  T d3v(void);
  typedef T d3t();
  extern T f3();
  __typeof(*T()) f4(); // expected-warning {{empty parentheses interpreted as a function declaration}} expected-note {{replace parentheses with an initializer}}
  typedef void *V;
  __typeof(*V()) f5();
  T multi1,
    multi2(); // expected-warning {{empty parentheses interpreted as a function declaration}} expected-note {{replace parentheses with an initializer}}
  T(d)[5]; // expected-error {{redefinition of 'd'}}
  typeof(int[])(f) = { 1, 2 }; // expected-error {{extension used}}
  void(b)(int);
  int(d2) __attribute__(());
  if (int(a)=1) {}
  int(d3(int()));
}

struct RAII {
  RAII();
  ~RAII();
};

void func();
namespace N {
  struct S;

  void emptyParens() {
    RAII raii(); // expected-warning {{function declaration}} expected-note {{remove parentheses to declare a variable}}
    int a, b, c, d, e, // expected-note {{change this ',' to a ';' to call 'func'}}
    func(); // expected-warning {{function declaration}} expected-note {{replace parentheses with an initializer}}

    S s(); // expected-warning {{function declaration}}
  }
}

class C { };
void fn(int(C)) { } // void fn(int(*fp)(C c)) { } expected-note{{candidate function}}
                    // not: void fn(int C);
int g(C);

void foo() {
  fn(1); // expected-error {{no matching function}}
  fn(g); // OK
}
