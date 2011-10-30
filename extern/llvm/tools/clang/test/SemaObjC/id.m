// RUN: %clang_cc1 -fsyntax-only -verify %s

@protocol Foo;

Class T;
id<Foo> S;
id R;
void foo() {
  // Test assignment compatibility of Class and id.  No warning should be
  // produced.
  // rdar://6770142 - Class and id<foo> are compatible.
  S = T; // expected-warning {{incompatible pointer types assigning to 'id<Foo>' from 'Class'}}
  T = S; // expected-warning {{incompatible pointer types assigning to 'Class' from 'id<Foo>'}}
  R = T; T = R;
  R = S; S = R;
}

// Test attempt to redefine 'id' in an incompatible fashion.
typedef int id;  // FIXME: Decide how we want to deal with this (now that 'id' is more of a built-in type).
id b;

