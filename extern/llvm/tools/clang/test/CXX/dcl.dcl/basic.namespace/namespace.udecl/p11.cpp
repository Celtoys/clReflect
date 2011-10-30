// RUN: %clang_cc1 -fsyntax-only -verify %s

// C++03 [namespace.udecl]p11:
//   If a function declaration in namespace scope or block scope has
//   the same name and the same parameter types as a function
//   introduced by a using-declaration, the program is
//   ill-formed. [Note: two using-declarations may introduce functions
//   with the same name and the same parameter types. If, for a call
//   to an unqualified function name, function overload resolution
//   selects the functions introduced by such using-declarations, the
//   function call is ill-formed.

namespace test0 {
  namespace ns { void foo(); } // expected-note {{target of using declaration}}
  int foo(); // expected-note {{conflicting declaration}}
  using ns::foo; // expected-error {{target of using declaration conflicts with declaration already in scope}}
}

namespace test1 {
  namespace ns { void foo(); } // expected-note {{target of using declaration}}
  using ns::foo; //expected-note {{using declaration}}
  int foo(); // expected-error {{declaration conflicts with target of using declaration already in scope}}
}

namespace test2 {
  namespace ns { void foo(); } // expected-note 2 {{target of using declaration}}
  void test0() {
    int foo(); // expected-note {{conflicting declaration}}
    using ns::foo; // expected-error {{target of using declaration conflicts with declaration already in scope}}
  }

  void test1() {
    using ns::foo; //expected-note {{using declaration}}
    int foo(); // expected-error {{declaration conflicts with target of using declaration already in scope}}
  }
}

namespace test3 {
  namespace ns { void foo(); } // expected-note 2 {{target of using declaration}}
  class Test0 {
    void test() {
      int foo(); // expected-note {{conflicting declaration}}
      using ns::foo; // expected-error {{target of using declaration conflicts with declaration already in scope}}
    }
  };

  class Test1 {
    void test() {
      using ns::foo; //expected-note {{using declaration}}
      int foo(); // expected-error {{declaration conflicts with target of using declaration already in scope}}
    }
  };
}

namespace test4 {
  namespace ns { void foo(); } // expected-note 2 {{target of using declaration}}
  template <typename> class Test0 {
    void test() {
      int foo(); // expected-note {{conflicting declaration}}
      using ns::foo; // expected-error {{target of using declaration conflicts with declaration already in scope}}
    }
  };

  template <typename> class Test1 {
    void test() {
      using ns::foo; //expected-note {{using declaration}}
      int foo(); // expected-error {{declaration conflicts with target of using declaration already in scope}}
    }
  };
}

// FIXME: we should be able to diagnose both of these, but we can't.
// ...I'm actually not sure why we can diagnose either of them; it's
// probably a bug.
namespace test5 {
  namespace ns { void foo(int); } // expected-note {{target of using declaration}}
  template <typename T> class Test0 {
    void test() {
      int foo(T);
      using ns::foo;
    }
  };

  template <typename T> class Test1 {
    void test() {
      using ns::foo; // expected-note {{using declaration}}
      int foo(T); // expected-error {{declaration conflicts with target of using declaration already in scope}}
    }
  };

  template class Test0<int>;
  template class Test1<int>; // expected-note {{in instantiation of member function}}
}

