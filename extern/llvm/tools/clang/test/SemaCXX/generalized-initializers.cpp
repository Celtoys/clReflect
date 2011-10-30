// RUN: %clang_cc1 -std=c++11 -fsyntax-only -verify %s
// XFAIL: *

template <typename T, typename U>
struct same_type { static const bool value = false; };
template <typename T>
struct same_type<T, T> { static const bool value = true; };

namespace std {
  typedef decltype(sizeof(int)) size_t;

  // libc++'s implementation
  template <class _E>
  class initializer_list
  {
    const _E* __begin_;
    size_t    __size_;

    initializer_list(const _E* __b, size_t __s)
      : __begin_(__b),
        __size_(__s)
    {}

  public:
    typedef _E        value_type;
    typedef const _E& reference;
    typedef const _E& const_reference;
    typedef size_t    size_type;

    typedef const _E* iterator;
    typedef const _E* const_iterator;

    initializer_list() : __begin_(nullptr), __size_(0) {}

    size_t    size()  const {return __size_;}
    const _E* begin() const {return __begin_;}
    const _E* end()   const {return __begin_ + __size_;}
  };
}

namespace integral {

  int function_call() {
    void takes_int(int);
    takes_int({1});
  }

  void inline_init() {
    (void) int{1};
    (void) new int{1};
  }

  void initializer_list() {
    std::initializer_list<int> il = { 1, 2, 3 };
    std::initializer_list<double> dl = { 1.0, 2.0, 3 };
    auto l = {1, 2, 3, 4};
    static_assert(same_type<decltype(l), std::initializer_list<int>>::value, "");
    auto bl = {1, 2.0}; // expected-error {{cannot deduce}}

    for (int i : {1, 2, 3, 4}) {}
  }

}

namespace objects {

  struct X1 { X1(int); };
  struct X2 { explicit X2(int); };

  template <int N>
  struct A {
    A() { static_assert(N == 0, ""); }
    A(int, double) { static_assert(N == 1, ""); }
    A(std::initializer_list<int>) { static_assert(N == 3, ""); }
  };

  template <int N>
  struct D {
    D(std::initializer_list<int>) { static_assert(N == 0, ""); } // expected-note 1 {{candidate}}
    D(std::initializer_list<double>) { static_assert(N == 1, ""); } // expected-note 1 {{candidate}}
  };

  template <int N>
  struct E {
    E(int, int) { static_assert(N == 0, ""); }
    E(X1, int) { static_assert(N == 1, ""); }
  };

  void overload_resolution() {
    { A<0> a{}; }
    { A<0> a = {}; }
    // Narrowing conversions don't affect viability. The next two choose
    // the initializer_list constructor.
    { A<3> a{1, 1.0}; } // expected-error {{narrowing conversion}}
    { A<3> a = {1, 1.0}; } // expected-error {{narrowing conversion}}
    { A<3> a{1, 2, 3, 4, 5, 6, 7, 8}; }
    { A<3> a = {1, 2, 3, 4, 5, 6, 7, 8}; }
    { A<3> a{1, 2, 3, 4, 5, 6, 7, 8}; }
    { A<3> a{1, 2}; }

    { D<0> d{1, 2, 3}; }
    { D<1> d{1.0, 2.0, 3.0}; }
    { D<-1> d{1, 2.0}; } // expected-error {{ambiguous}}

    { E<0> e{1, 2}; }
  }

  void explicit_implicit() {
    { X1 x{0}; }
    { X1 x = {0}; }
    { X2 x{0}; }
    { X2 x = {0}; } // expected-error {{explicit}}
  }

  struct C {
    C();
    C(int, double);
    C(int, int);
    C(std::initializer_list<int>);

    int operator[](C);
  };

  C function_call() {
    void takes_C(C);
    takes_C({1, 1.0});

    C c;
    c[{1, 1.0}];

    return {1, 1.0};
  }

  void inline_init() {
    (void) A<1>{1, 1.0};
    (void) new A<1>{1, 1.0};
  }

  struct B {
    B(C, int, C);
  };

  void nested_init() {
    B b{{1, 1.0}, 2, {3, 4, 5, 6, 7}};
  }
}

namespace litb {

  // invalid
  struct A { int a[2]; A():a({1, 2}) { } }; // expected-error {{}}

  // invalid
  int a({0}); // expected-error {{}}

  // invalid
  int const &b({0}); // expected-error {{}}

  struct C { explicit C(int, int); C(int, long); };

  // invalid
  C c({1, 2}); // expected-error {{}}

  // valid (by copy constructor).
  C d({1, 2L}); // expected-error {{}}

  // valid
  C e{1, 2};

  struct B { 
    template<typename ...T>
    B(std::initializer_list<int>, T ...); 
  };

  // invalid (the first phase only considers init-list ctors)
  // (for the second phase, no constructor is viable)
  B f{1, 2, 3};

  // valid (T deduced to <>).
  B g({1, 2, 3});

}

namespace aggregate {
  // Direct list initialization does NOT allow braces to be elided!
  struct S {
    int ar[2];
    struct T {
      int i1;
      int i2;
    } t;
    struct U {
      int i1;
    } u[2];
    struct V {
      int var[2];
    } v;
  };

  void test() {
    S s1 = { 1, 2, 3 ,4, 5, 6, 7, 8 }; // no-error
    S s2{ {1, 2}, {3, 4}, { {5}, {6} }, { {7, 8} } }; // completely braced
    S s3{ 1, 2, 3, 4, 5, 6 }; // xpected-error
    S s4{ {1, 2}, {3, 4}, {5, 6}, { {7, 8} } }; // xpected-error
    S s5{ {1, 2}, {3, 4}, { {5}, {6} }, {7, 8} }; // xpected-error
  }
}
