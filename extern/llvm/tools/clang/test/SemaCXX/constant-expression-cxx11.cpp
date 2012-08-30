// RUN: %clang_cc1 -triple i686-linux -Wno-string-plus-int -fsyntax-only -verify -std=c++11 -pedantic %s -Wno-comment

namespace StaticAssertFoldTest {

int x;
static_assert(++x, "test"); // expected-error {{not an integral constant expression}}
static_assert(false, "test"); // expected-error {{test}}

}

typedef decltype(sizeof(char)) size_t;

template<typename T> constexpr T id(const T &t) { return t; }
template<typename T> constexpr T min(const T &a, const T &b) {
  return a < b ? a : b;
}
template<typename T> constexpr T max(const T &a, const T &b) {
  return a < b ? b : a;
}
template<typename T, size_t N> constexpr T *begin(T (&xs)[N]) { return xs; }
template<typename T, size_t N> constexpr T *end(T (&xs)[N]) { return xs + N; }

struct MemberZero {
  constexpr int zero() { return 0; }
};

namespace DerivedToVBaseCast {

  struct U { int n; };
  struct V : U { int n; };
  struct A : virtual V { int n; };
  struct Aa { int n; };
  struct B : virtual A, Aa {};
  struct C : virtual A, Aa {};
  struct D : B, C {};

  D d;
  constexpr B *p = &d;
  constexpr C *q = &d;

  static_assert((void*)p != (void*)q, "");
  static_assert((A*)p == (A*)q, "");
  static_assert((Aa*)p != (Aa*)q, "");

  constexpr B &pp = d;
  constexpr C &qq = d;
  static_assert((void*)&pp != (void*)&qq, "");
  static_assert(&(A&)pp == &(A&)qq, "");
  static_assert(&(Aa&)pp != &(Aa&)qq, "");

  constexpr V *v = p;
  constexpr V *w = q;
  constexpr V *x = (A*)p;
  static_assert(v == w, "");
  static_assert(v == x, "");

  static_assert((U*)&d == p, "");
  static_assert((U*)&d == q, "");
  static_assert((U*)&d == v, "");
  static_assert((U*)&d == w, "");
  static_assert((U*)&d == x, "");

  struct X {};
  struct Y1 : virtual X {};
  struct Y2 : X {};
  struct Z : Y1, Y2 {};
  Z z;
  static_assert((X*)(Y1*)&z != (X*)(Y2*)&z, "");
}

namespace ConstCast {

constexpr int n1 = 0;
constexpr int n2 = const_cast<int&>(n1);
constexpr int *n3 = const_cast<int*>(&n1);
constexpr int n4 = *const_cast<int*>(&n1);
constexpr const int * const *n5 = const_cast<const int* const*>(&n3);
constexpr int **n6 = const_cast<int**>(&n3);
constexpr int n7 = **n5;
constexpr int n8 = **n6;

}

namespace TemplateArgumentConversion {
  template<int n> struct IntParam {};

  using IntParam0 = IntParam<0>;
  using IntParam0 = IntParam<id(0)>;
  using IntParam0 = IntParam<MemberZero().zero>; // expected-error {{did you mean to call it with no arguments?}}
}

namespace CaseStatements {
  void f(int n) {
    switch (n) {
    case MemberZero().zero: // expected-error {{did you mean to call it with no arguments?}} expected-note {{previous}}
    case id(0): // expected-error {{duplicate case value '0'}}
      return;
    }
  }
}

extern int &Recurse1;
int &Recurse2 = Recurse1; // expected-note {{declared here}}
int &Recurse1 = Recurse2;
constexpr int &Recurse3 = Recurse2; // expected-error {{must be initialized by a constant expression}} expected-note {{initializer of 'Recurse2' is not a constant expression}}

extern const int RecurseA;
const int RecurseB = RecurseA; // expected-note {{declared here}}
const int RecurseA = 10;
constexpr int RecurseC = RecurseB; // expected-error {{must be initialized by a constant expression}} expected-note {{initializer of 'RecurseB' is not a constant expression}}

namespace MemberEnum {
  struct WithMemberEnum {
    enum E { A = 42 };
  } wme;

  static_assert(wme.A == 42, "");
}

namespace DefaultArguments {

const int z = int();
constexpr int Sum(int a = 0, const int &b = 0, const int *c = &z, char d = 0) {
  return a + b + *c + d;
}
const int four = 4;
constexpr int eight = 8;
constexpr const int twentyseven = 27;
static_assert(Sum() == 0, "");
static_assert(Sum(1) == 1, "");
static_assert(Sum(1, four) == 5, "");
static_assert(Sum(1, eight, &twentyseven) == 36, "");
static_assert(Sum(1, 2, &four, eight) == 15, "");

}

namespace Ellipsis {

// Note, values passed through an ellipsis can't actually be used.
constexpr int F(int a, ...) { return a; }
static_assert(F(0) == 0, "");
static_assert(F(1, 0) == 1, "");
static_assert(F(2, "test") == 2, "");
static_assert(F(3, &F) == 3, "");
int k = 0; // expected-note {{here}}
static_assert(F(4, k) == 3, ""); // expected-error {{constant expression}} expected-note {{read of non-const variable 'k'}}

}

namespace Recursion {
  constexpr int fib(int n) { return n > 1 ? fib(n-1) + fib(n-2) : n; }
  static_assert(fib(11) == 89, "");

  constexpr int gcd_inner(int a, int b) {
    return b == 0 ? a : gcd_inner(b, a % b);
  }
  constexpr int gcd(int a, int b) {
    return gcd_inner(max(a, b), min(a, b));
  }

  static_assert(gcd(1749237, 5628959) == 7, "");
}

namespace FunctionCast {
  // When folding, we allow functions to be cast to different types. Such
  // cast functions cannot be called, even if they're constexpr.
  constexpr int f() { return 1; }
  typedef double (*DoubleFn)();
  typedef int (*IntFn)();
  int a[(int)DoubleFn(f)()]; // expected-error {{variable length array}} expected-warning{{C99 feature}}
  int b[(int)IntFn(f)()];    // ok
}

namespace StaticMemberFunction {
  struct S {
    static constexpr int k = 42;
    static constexpr int f(int n) { return n * k + 2; }
  } s;

  constexpr int n = s.f(19);
  static_assert(S::f(19) == 800, "");
  static_assert(s.f(19) == 800, "");
  static_assert(n == 800, "");

  constexpr int (*sf1)(int) = &S::f;
  constexpr int (*sf2)(int) = &s.f;
  constexpr const int *sk = &s.k;
}

namespace ParameterScopes {

  const int k = 42;
  constexpr const int &ObscureTheTruth(const int &a) { return a; }
  constexpr const int &MaybeReturnJunk(bool b, const int a) { // expected-note 2{{declared here}}
    return ObscureTheTruth(b ? a : k);
  }
  static_assert(MaybeReturnJunk(false, 0) == 42, ""); // ok
  constexpr int a = MaybeReturnJunk(true, 0); // expected-error {{constant expression}} expected-note {{read of variable whose lifetime has ended}}

  constexpr const int MaybeReturnNonstaticRef(bool b, const int a) {
    return ObscureTheTruth(b ? a : k);
  }
  static_assert(MaybeReturnNonstaticRef(false, 0) == 42, ""); // ok
  constexpr int b = MaybeReturnNonstaticRef(true, 0); // ok

  constexpr int InternalReturnJunk(int n) {
    return MaybeReturnJunk(true, n); // expected-note {{read of variable whose lifetime has ended}}
  }
  constexpr int n3 = InternalReturnJunk(0); // expected-error {{must be initialized by a constant expression}} expected-note {{in call to 'InternalReturnJunk(0)'}}

  constexpr int LToR(int &n) { return n; }
  constexpr int GrabCallersArgument(bool which, int a, int b) {
    return LToR(which ? b : a);
  }
  static_assert(GrabCallersArgument(false, 1, 2) == 1, "");
  static_assert(GrabCallersArgument(true, 4, 8) == 8, "");

}

namespace Pointers {

  constexpr int f(int n, const int *a, const int *b, const int *c) {
    return n == 0 ? 0 : *a + f(n-1, b, c, a);
  }

  const int x = 1, y = 10, z = 100;
  static_assert(f(23, &x, &y, &z) == 788, "");

  constexpr int g(int n, int a, int b, int c) {
    return f(n, &a, &b, &c);
  }
  static_assert(g(23, x, y, z) == 788, "");

}

namespace FunctionPointers {

  constexpr int Double(int n) { return 2 * n; }
  constexpr int Triple(int n) { return 3 * n; }
  constexpr int Twice(int (*F)(int), int n) { return F(F(n)); }
  constexpr int Quadruple(int n) { return Twice(Double, n); }
  constexpr auto Select(int n) -> int (*)(int) {
    return n == 2 ? &Double : n == 3 ? &Triple : n == 4 ? &Quadruple : 0;
  }
  constexpr int Apply(int (*F)(int), int n) { return F(n); } // expected-note {{subexpression}}

  static_assert(1 + Apply(Select(4), 5) + Apply(Select(3), 7) == 42, "");

  constexpr int Invalid = Apply(Select(0), 0); // expected-error {{must be initialized by a constant expression}} expected-note {{in call to 'Apply(0, 0)'}}

}

namespace PointerComparison {

int x, y;
static_assert(&x == &y, "false"); // expected-error {{false}}
static_assert(&x != &y, "");
constexpr bool g1 = &x == &y;
constexpr bool g2 = &x != &y;
constexpr bool g3 = &x <= &y; // expected-error {{must be initialized by a constant expression}}
constexpr bool g4 = &x >= &y; // expected-error {{must be initialized by a constant expression}}
constexpr bool g5 = &x < &y; // expected-error {{must be initialized by a constant expression}}
constexpr bool g6 = &x > &y; // expected-error {{must be initialized by a constant expression}}

struct S { int x, y; } s;
static_assert(&s.x == &s.y, "false"); // expected-error {{false}}
static_assert(&s.x != &s.y, "");
static_assert(&s.x <= &s.y, "");
static_assert(&s.x >= &s.y, "false"); // expected-error {{false}}
static_assert(&s.x < &s.y, "");
static_assert(&s.x > &s.y, "false"); // expected-error {{false}}

static_assert(0 == &y, "false"); // expected-error {{false}}
static_assert(0 != &y, "");
constexpr bool n3 = 0 <= &y; // expected-error {{must be initialized by a constant expression}}
constexpr bool n4 = 0 >= &y; // expected-error {{must be initialized by a constant expression}}
constexpr bool n5 = 0 < &y; // expected-error {{must be initialized by a constant expression}}
constexpr bool n6 = 0 > &y; // expected-error {{must be initialized by a constant expression}}

static_assert(&x == 0, "false"); // expected-error {{false}}
static_assert(&x != 0, "");
constexpr bool n9 = &x <= 0; // expected-error {{must be initialized by a constant expression}}
constexpr bool n10 = &x >= 0; // expected-error {{must be initialized by a constant expression}}
constexpr bool n11 = &x < 0; // expected-error {{must be initialized by a constant expression}}
constexpr bool n12 = &x > 0; // expected-error {{must be initialized by a constant expression}}

static_assert(&x == &x, "");
static_assert(&x != &x, "false"); // expected-error {{false}}
static_assert(&x <= &x, "");
static_assert(&x >= &x, "");
static_assert(&x < &x, "false"); // expected-error {{false}}
static_assert(&x > &x, "false"); // expected-error {{false}}

constexpr S* sptr = &s;
constexpr bool dyncast = sptr == dynamic_cast<S*>(sptr); // expected-error {{constant expression}} expected-note {{dynamic_cast}}

struct U {};
struct Str {
  int a : dynamic_cast<S*>(sptr) == dynamic_cast<S*>(sptr); // \
    expected-warning {{not an integral constant expression}} \
    expected-note {{dynamic_cast is not allowed in a constant expression}}
  int b : reinterpret_cast<S*>(sptr) == reinterpret_cast<S*>(sptr); // \
    expected-warning {{not an integral constant expression}} \
    expected-note {{reinterpret_cast is not allowed in a constant expression}}
  int c : (S*)(long)(sptr) == (S*)(long)(sptr); // \
    expected-warning {{not an integral constant expression}} \
    expected-note {{cast which performs the conversions of a reinterpret_cast is not allowed in a constant expression}}
  int d : (S*)(42) == (S*)(42); // \
    expected-warning {{not an integral constant expression}} \
    expected-note {{cast which performs the conversions of a reinterpret_cast is not allowed in a constant expression}}
  int e : (Str*)(sptr) == (Str*)(sptr); // \
    expected-warning {{not an integral constant expression}} \
    expected-note {{cast which performs the conversions of a reinterpret_cast is not allowed in a constant expression}}
  int f : &(U&)(*sptr) == &(U&)(*sptr); // \
    expected-warning {{not an integral constant expression}} \
    expected-note {{cast which performs the conversions of a reinterpret_cast is not allowed in a constant expression}}
  int g : (S*)(void*)(sptr) == sptr; // \
    expected-warning {{not an integral constant expression}} \
    expected-note {{cast from 'void *' is not allowed in a constant expression}}
};

extern char externalvar[];
constexpr bool constaddress = (void *)externalvar == (void *)0x4000UL; // expected-error {{must be initialized by a constant expression}}
constexpr bool litaddress = "foo" == "foo"; // expected-error {{must be initialized by a constant expression}} expected-warning {{unspecified}}
static_assert(0 != "foo", "");

}

namespace MaterializeTemporary {

constexpr int f(const int &r) { return r; }
constexpr int n = f(1);

constexpr bool same(const int &a, const int &b) { return &a == &b; }
constexpr bool sameTemporary(const int &n) { return same(n, n); }

static_assert(n, "");
static_assert(!same(4, 4), "");
static_assert(same(n, n), "");
static_assert(sameTemporary(9), "");

}

constexpr int strcmp_ce(const char *p, const char *q) {
  return (!*p || *p != *q) ? *p - *q : strcmp_ce(p+1, q+1);
}

namespace StringLiteral {

template<typename Char>
constexpr int MangleChars(const Char *p) {
  return *p + 3 * (*p ? MangleChars(p+1) : 0);
}

static_assert(MangleChars("constexpr!") == 1768383, "");
static_assert(MangleChars(u8"constexpr!") == 1768383, "");
static_assert(MangleChars(L"constexpr!") == 1768383, "");
static_assert(MangleChars(u"constexpr!") == 1768383, "");
static_assert(MangleChars(U"constexpr!") == 1768383, "");

constexpr char c0 = "nought index"[0];
constexpr char c1 = "nice index"[10];
constexpr char c2 = "nasty index"[12]; // expected-error {{must be initialized by a constant expression}} expected-warning {{is past the end}} expected-note {{read of dereferenced one-past-the-end pointer}}
constexpr char c3 = "negative index"[-1]; // expected-error {{must be initialized by a constant expression}} expected-warning {{is before the beginning}} expected-note {{cannot refer to element -1 of array of 15 elements}}
constexpr char c4 = ((char*)(int*)"no reinterpret_casts allowed")[14]; // expected-error {{must be initialized by a constant expression}} expected-note {{cast which performs the conversions of a reinterpret_cast}}

constexpr const char *p = "test" + 2;
static_assert(*p == 's', "");

constexpr const char *max_iter(const char *a, const char *b) {
  return *a < *b ? b : a;
}
constexpr const char *max_element(const char *a, const char *b) {
  return (a+1 >= b) ? a : max_iter(a, max_element(a+1, b));
}

constexpr char str[] = "the quick brown fox jumped over the lazy dog";
constexpr const char *max = max_element(begin(str), end(str));
static_assert(*max == 'z', "");
static_assert(max == str + 38, "");

static_assert(strcmp_ce("hello world", "hello world") == 0, "");
static_assert(strcmp_ce("hello world", "hello clang") > 0, "");
static_assert(strcmp_ce("constexpr", "test") < 0, "");
static_assert(strcmp_ce("", " ") < 0, "");

struct S {
  int n : "foo"[4]; // expected-error {{constant expression}} expected-note {{read of dereferenced one-past-the-end pointer is not allowed in a constant expression}}
};

struct T {
  char c[6];
  constexpr T() : c{"foo"} {}
};
constexpr T t;

static_assert(t.c[0] == 'f', "");
static_assert(t.c[1] == 'o', "");
static_assert(t.c[2] == 'o', "");
static_assert(t.c[3] == 0, "");
static_assert(t.c[4] == 0, "");
static_assert(t.c[5] == 0, "");
static_assert(t.c[6] == 0, ""); // expected-error {{constant expression}} expected-note {{one-past-the-end}}

struct U {
  wchar_t chars[6];
  int n;
} constexpr u = { { L"test" }, 0 };
static_assert(u.chars[2] == L's', "");

struct V {
  char c[4];
  constexpr V() : c("hi!") {}
};
static_assert(V().c[1] == "i"[0], "");

}

namespace Array {

template<typename Iter>
constexpr auto Sum(Iter begin, Iter end) -> decltype(+*begin) {
  return begin == end ? 0 : *begin + Sum(begin+1, end);
}

constexpr int xs[] = { 1, 2, 3, 4, 5 };
constexpr int ys[] = { 5, 4, 3, 2, 1 };
constexpr int sum_xs = Sum(begin(xs), end(xs));
static_assert(sum_xs == 15, "");

constexpr int ZipFoldR(int (*F)(int x, int y, int c), int n,
                       const int *xs, const int *ys, int c) {
  return n ? F(
               *xs, // expected-note {{read of dereferenced one-past-the-end pointer}}
               *ys,
               ZipFoldR(F, n-1, xs+1, ys+1, c)) // \
      expected-note {{in call to 'ZipFoldR(&SubMul, 2, &xs[4], &ys[4], 1)'}} \
      expected-note {{in call to 'ZipFoldR(&SubMul, 1, &xs[5], &ys[5], 1)'}}
           : c;
}
constexpr int MulAdd(int x, int y, int c) { return x * y + c; }
constexpr int InnerProduct = ZipFoldR(MulAdd, 5, xs, ys, 0);
static_assert(InnerProduct == 35, "");

constexpr int SubMul(int x, int y, int c) { return (x - y) * c; }
constexpr int DiffProd = ZipFoldR(SubMul, 2, xs+3, ys+3, 1);
static_assert(DiffProd == 8, "");
static_assert(ZipFoldR(SubMul, 3, xs+3, ys+3, 1), ""); // \
      expected-error {{constant expression}} \
      expected-note {{in call to 'ZipFoldR(&SubMul, 3, &xs[3], &ys[3], 1)'}}

constexpr const int *p = xs + 3;
constexpr int xs4 = p[1]; // ok
constexpr int xs5 = p[2]; // expected-error {{constant expression}} expected-note {{read of dereferenced one-past-the-end pointer}}
constexpr int xs6 = p[3]; // expected-error {{constant expression}} expected-note {{cannot refer to element 6}}
constexpr int xs0 = p[-3]; // ok
constexpr int xs_1 = p[-4]; // expected-error {{constant expression}} expected-note {{cannot refer to element -1}}

constexpr int zs[2][2][2][2] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static_assert(zs[0][0][0][0] == 1, "");
static_assert(zs[1][1][1][1] == 16, "");
static_assert(zs[0][0][0][2] == 3, ""); // expected-error {{constant expression}} expected-note {{read of dereferenced one-past-the-end pointer}}
static_assert((&zs[0][0][0][2])[-1] == 2, "");
static_assert(**(**(zs + 1) + 1) == 11, "");
static_assert(*(&(&(*(*&(&zs[2] - 1)[0] + 2 - 2))[2])[-1][-1] + 1) == 11, ""); // expected-error {{constant expression}} expected-note {{cannot refer to element -1 of array of 2 elements in a constant expression}}
static_assert(*(&(&(*(*&(&zs[2] - 1)[0] + 2 - 2))[2])[-1][2] - 2) == 11, "");
constexpr int err_zs_1_2_0_0 = zs[1][2][0][0]; // expected-error {{constant expression}} expected-note {{cannot access array element of pointer past the end}}

constexpr int fail(const int &p) {
  return (&p)[64]; // expected-note {{cannot refer to element 64 of array of 2 elements}}
}
static_assert(fail(*(&(&(*(*&(&zs[2] - 1)[0] + 2 - 2))[2])[-1][2] - 2)) == 11, ""); // \
expected-error {{static_assert expression is not an integral constant expression}} \
expected-note {{in call to 'fail(zs[1][0][1][0])'}}

constexpr int arr[40] = { 1, 2, 3, [8] = 4 }; // expected-warning {{C99 feature}}
constexpr int SumNonzero(const int *p) {
  return *p + (*p ? SumNonzero(p+1) : 0);
}
constexpr int CountZero(const int *p, const int *q) {
  return p == q ? 0 : (*p == 0) + CountZero(p+1, q);
}
static_assert(SumNonzero(arr) == 6, "");
static_assert(CountZero(arr, arr + 40) == 36, "");

struct ArrayElem {
  constexpr ArrayElem() : n(0) {}
  int n;
  constexpr int f() { return n; }
};
struct ArrayRVal {
  constexpr ArrayRVal() {}
  ArrayElem elems[10];
};
static_assert(ArrayRVal().elems[3].f() == 0, "");

}

namespace DependentValues {

struct I { int n; typedef I V[10]; };
I::V x, y;
template<bool B> struct S {
  int k;
  void f() {
    I::V &cells = B ? x : y;
    I &i = cells[k];
    switch (i.n) {}
  }
};

}

namespace Class {

struct A { constexpr A(int a, int b) : k(a + b) {} int k; };
constexpr int fn(const A &a) { return a.k; }
static_assert(fn(A(4,5)) == 9, "");

struct B { int n; int m; } constexpr b = { 0, b.n }; // expected-warning {{uninitialized}}
struct C {
  constexpr C(C *this_) : m(42), n(this_->m) {} // ok
  int m, n;
};
struct D {
  C c;
  constexpr D() : c(&c) {}
};
static_assert(D().c.n == 42, "");

struct E {
  constexpr E() : p(&p) {}
  void *p;
};
constexpr const E &e1 = E(); // expected-error {{constant expression}} expected-note {{reference to temporary is not a constant expression}} expected-note {{temporary created here}}
// This is a constant expression if we elide the copy constructor call, and
// is not a constant expression if we don't! But we do, so it is.
constexpr E e2 = E();
static_assert(e2.p == &e2.p, "");
constexpr E e3;
static_assert(e3.p == &e3.p, "");

extern const class F f;
struct F {
  constexpr F() : p(&f.p) {}
  const void *p;
};
constexpr F f;

struct G {
  struct T {
    constexpr T(T *p) : u1(), u2(p) {}
    union U1 {
      constexpr U1() {}
      int a, b = 42;
    } u1;
    union U2 {
      constexpr U2(T *p) : c(p->u1.b) {}
      int c, d;
    } u2;
  } t;
  constexpr G() : t(&t) {}
} constexpr g;

static_assert(g.t.u1.a == 42, ""); // expected-error {{constant expression}} expected-note {{read of member 'a' of union with active member 'b'}}
static_assert(g.t.u1.b == 42, "");
static_assert(g.t.u2.c == 42, "");
static_assert(g.t.u2.d == 42, ""); // expected-error {{constant expression}} expected-note {{read of member 'd' of union with active member 'c'}}

struct S {
  int a, b;
  const S *p;
  double d;
  const char *q;

  constexpr S(int n, const S *p) : a(5), b(n), p(p), d(n), q("hello") {}
};

S global(43, &global);

static_assert(S(15, &global).b == 15, "");

constexpr bool CheckS(const S &s) {
  return s.a == 5 && s.b == 27 && s.p == &global && s.d == 27. && s.q[3] == 'l';
}
static_assert(CheckS(S(27, &global)), "");

struct Arr {
  char arr[3];
  constexpr Arr() : arr{'x', 'y', 'z'} {}
};
constexpr int hash(Arr &&a) {
  return a.arr[0] + a.arr[1] * 0x100 + a.arr[2] * 0x10000;
}
constexpr int k = hash(Arr());
static_assert(k == 0x007a7978, "");


struct AggregateInit {
  const char &c;
  int n;
  double d;
  int arr[5];
  void *p;
};

constexpr AggregateInit agg1 = { "hello"[0] };

static_assert(strcmp_ce(&agg1.c, "hello") == 0, "");
static_assert(agg1.n == 0, "");
static_assert(agg1.d == 0.0, "");
static_assert(agg1.arr[-1] == 0, ""); // expected-error {{constant expression}} expected-note {{cannot refer to element -1}}
static_assert(agg1.arr[0] == 0, "");
static_assert(agg1.arr[4] == 0, "");
static_assert(agg1.arr[5] == 0, ""); // expected-error {{constant expression}} expected-note {{read of dereferenced one-past-the-end}}
static_assert(agg1.p == nullptr, "");

static constexpr const unsigned char uc[] = { "foo" };
static_assert(uc[0] == 'f', "");
static_assert(uc[3] == 0, "");

namespace SimpleDerivedClass {

struct B {
  constexpr B(int n) : a(n) {}
  int a;
};
struct D : B {
  constexpr D(int n) : B(n) {}
};
constexpr D d(3);
static_assert(d.a == 3, "");

}

struct Bottom { constexpr Bottom() {} };
struct Base : Bottom {
  constexpr Base(int a = 42, const char *b = "test") : a(a), b(b) {}
  int a;
  const char *b;
};
struct Base2 : Bottom {
  constexpr Base2(const int &r) : r(r) {}
  int q = 123;
  const int &r;
};
struct Derived : Base, Base2 {
  constexpr Derived() : Base(76), Base2(a) {}
  int c = r + b[1];
};

constexpr bool operator==(const Base &a, const Base &b) {
  return a.a == b.a && strcmp_ce(a.b, b.b) == 0;
}

constexpr Base base;
constexpr Base base2(76);
constexpr Derived derived;
static_assert(derived.a == 76, "");
static_assert(derived.b[2] == 's', "");
static_assert(derived.c == 76 + 'e', "");
static_assert(derived.q == 123, "");
static_assert(derived.r == 76, "");
static_assert(&derived.r == &derived.a, "");

static_assert(!(derived == base), "");
static_assert(derived == base2, "");

constexpr Bottom &bot1 = (Base&)derived;
constexpr Bottom &bot2 = (Base2&)derived;
static_assert(&bot1 != &bot2, "");

constexpr Bottom *pb1 = (Base*)&derived;
constexpr Bottom *pb2 = (Base2*)&derived;
static_assert(&pb1 != &pb2, "");
static_assert(pb1 == &bot1, "");
static_assert(pb2 == &bot2, "");

constexpr Base2 &fail = (Base2&)bot1; // expected-error {{constant expression}} expected-note {{cannot cast object of dynamic type 'const Class::Derived' to type 'Class::Base2'}}
constexpr Base &fail2 = (Base&)*pb2; // expected-error {{constant expression}} expected-note {{cannot cast object of dynamic type 'const Class::Derived' to type 'Class::Base'}}
constexpr Base2 &ok2 = (Base2&)bot2;
static_assert(&ok2 == &derived, "");

constexpr Base2 *pfail = (Base2*)pb1; // expected-error {{constant expression}} expected-note {{cannot cast object of dynamic type 'const Class::Derived' to type 'Class::Base2'}}
constexpr Base *pfail2 = (Base*)&bot2; // expected-error {{constant expression}} expected-note {{cannot cast object of dynamic type 'const Class::Derived' to type 'Class::Base'}}
constexpr Base2 *pok2 = (Base2*)pb2;
static_assert(pok2 == &derived, "");
static_assert(&ok2 == pok2, "");
static_assert((Base2*)(Derived*)(Base*)pb1 == pok2, "");
static_assert((Derived*)(Base*)pb1 == (Derived*)pok2, "");

constexpr Base *nullB = 42 - 6 * 7;
static_assert((Bottom*)nullB == 0, "");
static_assert((Derived*)nullB == 0, "");
static_assert((void*)(Bottom*)nullB == (void*)(Derived*)nullB, "");

namespace ConversionOperators {

struct T {
  constexpr T(int n) : k(5*n - 3) {}
  constexpr operator int() { return k; }
  int k;
};

struct S {
  constexpr S(int n) : k(2*n + 1) {}
  constexpr operator int() { return k; }
  constexpr operator T() { return T(k); }
  int k;
};

constexpr bool check(T a, T b) { return a == b.k; }

static_assert(S(5) == 11, "");
static_assert(check(S(5), 11), "");

}

}

namespace Temporaries {

struct S {
  constexpr S() {}
  constexpr int f();
};
struct T : S {
  constexpr T(int n) : S(), n(n) {}
  int n;
};
constexpr int S::f() {
  // 'this' must be the postfix-expression in a class member access expression,
  // so we can't just use
  //   return static_cast<T*>(this)->n;
  return this->*(int(S::*))&T::n;
}
// The T temporary is implicitly cast to an S subobject, but we can recover the
// T full-object via a base-to-derived cast, or a derived-to-base-casted member
// pointer.
static_assert(T(3).f() == 3, "");

constexpr int f(const S &s) {
  return static_cast<const T&>(s).n;
}
constexpr int n = f(T(5));
static_assert(f(T(5)) == 5, "");

constexpr bool b(int n) { return &n; }
static_assert(b(0), "");

}

namespace Union {

union U {
  int a;
  int b;
};

constexpr U u[4] = { { .a = 0 }, { .b = 1 }, { .a = 2 }, { .b = 3 } }; // expected-warning 4{{C99 feature}}
static_assert(u[0].a == 0, "");
static_assert(u[0].b, ""); // expected-error {{constant expression}} expected-note {{read of member 'b' of union with active member 'a'}}
static_assert(u[1].b == 1, "");
static_assert((&u[1].b)[1] == 2, ""); // expected-error {{constant expression}} expected-note {{read of dereferenced one-past-the-end pointer}}
static_assert(*(&(u[1].b) + 1 + 1) == 3, ""); // expected-error {{constant expression}} expected-note {{cannot refer to element 2 of non-array object}}
static_assert((&(u[1]) + 1 + 1)->b == 3, "");

constexpr U v = {};
static_assert(v.a == 0, "");

union Empty {};
constexpr Empty e = {};

// Make sure we handle trivial copy constructors for unions.
constexpr U x = {42};
constexpr U y = x;
static_assert(y.a == 42, "");
static_assert(y.b == 42, ""); // expected-error {{constant expression}} expected-note {{'b' of union with active member 'a'}}

}

namespace MemberPointer {
  struct A {
    constexpr A(int n) : n(n) {}
    int n;
    constexpr int f() { return n + 3; }
  };
  constexpr A a(7);
  static_assert(A(5).*&A::n == 5, "");
  static_assert((&a)->*&A::n == 7, "");
  static_assert((A(8).*&A::f)() == 11, "");
  static_assert(((&a)->*&A::f)() == 10, "");

  struct B : A {
    constexpr B(int n, int m) : A(n), m(m) {}
    int m;
    constexpr int g() { return n + m + 1; }
  };
  constexpr B b(9, 13);
  static_assert(B(4, 11).*&A::n == 4, "");
  static_assert(B(4, 11).*&B::m == 11, "");
  static_assert(B(4, 11).*(int(A::*))&B::m == 11, "");
  static_assert((&b)->*&A::n == 9, "");
  static_assert((&b)->*&B::m == 13, "");
  static_assert((&b)->*(int(A::*))&B::m == 13, "");
  static_assert((B(4, 11).*&A::f)() == 7, "");
  static_assert((B(4, 11).*&B::g)() == 16, "");
  static_assert((B(4, 11).*(int(A::*)()const)&B::g)() == 16, "");
  static_assert(((&b)->*&A::f)() == 12, "");
  static_assert(((&b)->*&B::g)() == 23, "");
  static_assert(((&b)->*(int(A::*)()const)&B::g)() == 23, "");

  struct S {
    constexpr S(int m, int n, int (S::*pf)() const, int S::*pn) :
      m(m), n(n), pf(pf), pn(pn) {}
    constexpr S() : m(), n(), pf(&S::f), pn(&S::n) {}

    constexpr int f() { return this->*pn; }
    virtual int g() const;

    int m, n;
    int (S::*pf)() const;
    int S::*pn;
  };

  constexpr int S::*pm = &S::m;
  constexpr int S::*pn = &S::n;
  constexpr int (S::*pf)() const = &S::f;
  constexpr int (S::*pg)() const = &S::g;

  constexpr S s(2, 5, &S::f, &S::m);

  static_assert((s.*&S::f)() == 2, "");
  static_assert((s.*s.pf)() == 2, "");

  static_assert(pf == &S::f, "");
  static_assert(pf == s.*&S::pf, "");
  static_assert(pm == &S::m, "");
  static_assert(pm != pn, "");
  static_assert(s.pn != pn, "");
  static_assert(s.pn == pm, "");
  static_assert(pg != nullptr, "");
  static_assert(pf != nullptr, "");
  static_assert((int S::*)nullptr == nullptr, "");
  static_assert(pg == pg, ""); // expected-error {{constant expression}} expected-note {{comparison of pointer to virtual member function 'g' has unspecified value}}
  static_assert(pf != pg, ""); // expected-error {{constant expression}} expected-note {{comparison of pointer to virtual member function 'g' has unspecified value}}

  template<int n> struct T : T<n-1> {};
  template<> struct T<0> { int n; };
  template<> struct T<30> : T<29> { int m; };

  T<17> t17;
  T<30> t30;

  constexpr int (T<10>::*deepn) = &T<0>::n;
  static_assert(&(t17.*deepn) == &t17.n, "");
  static_assert(deepn == &T<2>::n, "");

  constexpr int (T<15>::*deepm) = (int(T<10>::*))&T<30>::m;
  constexpr int *pbad = &(t17.*deepm); // expected-error {{constant expression}}
  static_assert(&(t30.*deepm) == &t30.m, "");
  static_assert(deepm == &T<50>::m, "");
  static_assert(deepm != deepn, "");

  constexpr T<5> *p17_5 = &t17;
  constexpr T<13> *p17_13 = (T<13>*)p17_5;
  constexpr T<23> *p17_23 = (T<23>*)p17_13; // expected-error {{constant expression}} expected-note {{cannot cast object of dynamic type 'T<17>' to type 'T<23>'}}
  static_assert(&(p17_5->*(int(T<3>::*))deepn) == &t17.n, "");
  static_assert(&(p17_13->*deepn) == &t17.n, "");
  constexpr int *pbad2 = &(p17_13->*(int(T<9>::*))deepm); // expected-error {{constant expression}}

  constexpr T<5> *p30_5 = &t30;
  constexpr T<23> *p30_23 = (T<23>*)p30_5;
  constexpr T<13> *p30_13 = p30_23;
  static_assert(&(p30_5->*(int(T<3>::*))deepn) == &t30.n, "");
  static_assert(&(p30_13->*deepn) == &t30.n, "");
  static_assert(&(p30_23->*deepn) == &t30.n, "");
  static_assert(&(p30_5->*(int(T<2>::*))deepm) == &t30.m, "");
  static_assert(&(((T<17>*)p30_13)->*deepm) == &t30.m, "");
  static_assert(&(p30_23->*deepm) == &t30.m, "");

  struct Base { int n; };
  template<int N> struct Mid : Base {};
  struct Derived : Mid<0>, Mid<1> {};
  static_assert(&Mid<0>::n == &Mid<1>::n, "");
  static_assert((int Derived::*)(int Mid<0>::*)&Mid<0>::n !=
                (int Derived::*)(int Mid<1>::*)&Mid<1>::n, "");
  static_assert(&Mid<0>::n == (int Mid<0>::*)&Base::n, "");
}

namespace ArrayBaseDerived {

  struct Base {
    constexpr Base() {}
    int n = 0;
  };
  struct Derived : Base {
    constexpr Derived() {}
    constexpr const int *f() { return &n; }
  };

  constexpr Derived a[10];
  constexpr Derived *pd3 = const_cast<Derived*>(&a[3]);
  constexpr Base *pb3 = const_cast<Derived*>(&a[3]);
  static_assert(pb3 == pd3, "");

  // pb3 does not point to an array element.
  constexpr Base *pb4 = pb3 + 1; // ok, one-past-the-end pointer.
  constexpr int pb4n = pb4->n; // expected-error {{constant expression}} expected-note {{cannot access field of pointer past the end}}
  constexpr Base *err_pb5 = pb3 + 2; // expected-error {{constant expression}} expected-note {{cannot refer to element 2}} expected-note {{here}}
  constexpr int err_pb5n = err_pb5->n; // expected-error {{constant expression}} expected-note {{initializer of 'err_pb5' is not a constant expression}}
  constexpr Base *err_pb2 = pb3 - 1; // expected-error {{constant expression}} expected-note {{cannot refer to element -1}} expected-note {{here}}
  constexpr int err_pb2n = err_pb2->n; // expected-error {{constant expression}} expected-note {{initializer of 'err_pb2'}}
  constexpr Base *pb3a = pb4 - 1;

  // pb4 does not point to a Derived.
  constexpr Derived *err_pd4 = (Derived*)pb4; // expected-error {{constant expression}} expected-note {{cannot access derived class of pointer past the end}}
  constexpr Derived *pd3a = (Derived*)pb3a;
  constexpr int pd3n = pd3a->n;

  // pd3a still points to the Derived array.
  constexpr Derived *pd6 = pd3a + 3;
  static_assert(pd6 == &a[6], "");
  constexpr Derived *pd9 = pd6 + 3;
  constexpr Derived *pd10 = pd6 + 4;
  constexpr int pd9n = pd9->n; // ok
  constexpr int err_pd10n = pd10->n; // expected-error {{constant expression}} expected-note {{cannot access base class of pointer past the end}}
  constexpr int pd0n = pd10[-10].n;
  constexpr int err_pdminus1n = pd10[-11].n; // expected-error {{constant expression}} expected-note {{cannot refer to element -1 of}}

  constexpr Base *pb9 = pd9;
  constexpr const int *(Base::*pfb)() const =
      static_cast<const int *(Base::*)() const>(&Derived::f);
  static_assert((pb9->*pfb)() == &a[9].n, "");
}

namespace Complex {

class complex {
  int re, im;
public:
  constexpr complex(int re = 0, int im = 0) : re(re), im(im) {}
  constexpr complex(const complex &o) : re(o.re), im(o.im) {}
  constexpr complex operator-() const { return complex(-re, -im); }
  friend constexpr complex operator+(const complex &l, const complex &r) {
    return complex(l.re + r.re, l.im + r.im);
  }
  friend constexpr complex operator-(const complex &l, const complex &r) {
    return l + -r;
  }
  friend constexpr complex operator*(const complex &l, const complex &r) {
    return complex(l.re * r.re - l.im * r.im, l.re * r.im + l.im * r.re);
  }
  friend constexpr bool operator==(const complex &l, const complex &r) {
    return l.re == r.re && l.im == r.im;
  }
  constexpr bool operator!=(const complex &r) const {
    return re != r.re || im != r.im;
  }
  constexpr int real() const { return re; }
  constexpr int imag() const { return im; }
};

constexpr complex i = complex(0, 1);
constexpr complex k = (3 + 4*i) * (6 - 4*i);
static_assert(complex(1,0).real() == 1, "");
static_assert(complex(1,0).imag() == 0, "");
static_assert(((complex)1).imag() == 0, "");
static_assert(k.real() == 34, "");
static_assert(k.imag() == 12, "");
static_assert(k - 34 == 12*i, "");
static_assert((complex)1 == complex(1), "");
static_assert((complex)1 != complex(0, 1), "");
static_assert(complex(1) == complex(1), "");
static_assert(complex(1) != complex(0, 1), "");
constexpr complex makeComplex(int re, int im) { return complex(re, im); }
static_assert(makeComplex(1,0) == complex(1), "");
static_assert(makeComplex(1,0) != complex(0, 1), "");

class complex_wrap : public complex {
public:
  constexpr complex_wrap(int re, int im = 0) : complex(re, im) {}
  constexpr complex_wrap(const complex_wrap &o) : complex(o) {}
};

static_assert((complex_wrap)1 == complex(1), "");
static_assert((complex)1 != complex_wrap(0, 1), "");
static_assert(complex(1) == complex_wrap(1), "");
static_assert(complex_wrap(1) != complex(0, 1), "");
constexpr complex_wrap makeComplexWrap(int re, int im) {
  return complex_wrap(re, im);
}
static_assert(makeComplexWrap(1,0) == complex(1), "");
static_assert(makeComplexWrap(1,0) != complex(0, 1), "");

}

namespace PR11595 {
  struct A { constexpr bool operator==(int x) { return true; } };
  struct B { B(); A& x; };
  static_assert(B().x == 3, "");  // expected-error {{constant expression}} expected-note {{non-literal type 'PR11595::B' cannot be used in a constant expression}}

  constexpr bool f(int k) { // expected-error {{constexpr function never produces a constant expression}}
    return B().x == k; // expected-note {{non-literal type 'PR11595::B' cannot be used in a constant expression}}
  }
}

namespace ExprWithCleanups {
  struct A { A(); ~A(); int get(); };
  constexpr int get(bool FromA) { return FromA ? A().get() : 1; }
  constexpr int n = get(false);
}

namespace Volatile {

volatile constexpr int n1 = 0; // expected-note {{here}}
volatile const int n2 = 0; // expected-note {{here}}
int n3 = 37; // expected-note {{declared here}}

constexpr int m1 = n1; // expected-error {{constant expression}} expected-note {{read of volatile-qualified type 'const volatile int'}}
constexpr int m2 = n2; // expected-error {{constant expression}} expected-note {{read of volatile-qualified type 'const volatile int'}}
constexpr int m1b = const_cast<const int&>(n1); // expected-error {{constant expression}} expected-note {{read of volatile object 'n1'}}
constexpr int m2b = const_cast<const int&>(n2); // expected-error {{constant expression}} expected-note {{read of volatile object 'n2'}}

struct T { int n; };
const T t = { 42 }; // expected-note {{declared here}}

constexpr int f(volatile int &&r) {
  return r; // expected-note {{read of volatile-qualified type 'volatile int'}}
}
constexpr int g(volatile int &&r) {
  return const_cast<int&>(r); // expected-note {{read of volatile temporary is not allowed in a constant expression}}
}
struct S {
  int j : f(0); // expected-error {{constant expression}} expected-note {{in call to 'f(0)'}}
  int k : g(0); // expected-error {{constant expression}} expected-note {{temporary created here}} expected-note {{in call to 'g(0)'}}
  int l : n3; // expected-error {{constant expression}} expected-note {{read of non-const variable}}
  int m : t.n; // expected-error {{constant expression}} expected-note {{read of non-constexpr variable}}
};

}

namespace ExternConstexpr {
  extern constexpr int n = 0;
  extern constexpr int m; // expected-error {{constexpr variable declaration must be a definition}}
  void f() {
    extern constexpr int i; // expected-error {{constexpr variable declaration must be a definition}}
    constexpr int j = 0;
    constexpr int k; // expected-error {{default initialization of an object of const type}}
  }
}

namespace ComplexConstexpr {
  constexpr _Complex float test1 = {};
  constexpr _Complex float test2 = {1};
  constexpr _Complex double test3 = {1,2};
  constexpr _Complex int test4 = {4};
  constexpr _Complex int test5 = 4;
  constexpr _Complex int test6 = {5,6};
  typedef _Complex float fcomplex;
  constexpr fcomplex test7 = fcomplex();

  constexpr const double &t2r = __real test3;
  constexpr const double &t2i = __imag test3;
  static_assert(&t2r + 1 == &t2i, "");
  static_assert(t2r == 1.0, "");
  static_assert(t2i == 2.0, "");
  constexpr const double *t2p = &t2r;
  static_assert(t2p[-1] == 0.0, ""); // expected-error {{constant expr}} expected-note {{cannot refer to element -1 of array of 2 elements}}
  static_assert(t2p[0] == 1.0, "");
  static_assert(t2p[1] == 2.0, "");
  static_assert(t2p[2] == 0.0, ""); // expected-error {{constant expr}} expected-note {{one-past-the-end pointer}}
  static_assert(t2p[3] == 0.0, ""); // expected-error {{constant expr}} expected-note {{cannot refer to element 3 of array of 2 elements}}
  constexpr _Complex float *p = 0;
  constexpr float pr = __real *p; // expected-error {{constant expr}} expected-note {{cannot access real component of null}}
  constexpr float pi = __imag *p; // expected-error {{constant expr}} expected-note {{cannot access imaginary component of null}}
  constexpr const _Complex double *q = &test3 + 1;
  constexpr double qr = __real *q; // expected-error {{constant expr}} expected-note {{cannot access real component of pointer past the end}}
  constexpr double qi = __imag *q; // expected-error {{constant expr}} expected-note {{cannot access imaginary component of pointer past the end}}

  static_assert(__real test6 == 5, "");
  static_assert(__imag test6 == 6, "");
  static_assert(&__imag test6 == &__real test6 + 1, "");
}

namespace InstantiateCaseStmt {
  template<int x> constexpr int f() { return x; }
  template<int x> int g(int c) { switch(c) { case f<x>(): return 1; } return 0; }
  int gg(int c) { return g<4>(c); }
}

namespace ConvertedConstantExpr {
  extern int &m;
  extern int &n;

  constexpr int k = 4;
  int &m = const_cast<int&>(k);

  // If we have nothing more interesting to say, ensure we don't produce a
  // useless note and instead just point to the non-constant subexpression.
  enum class E {
    em = m,
    en = n, // expected-error {{not a constant expression}}
    eo = (m +
          n // expected-error {{not a constant expression}}
          ),
    eq = reinterpret_cast<int>((int*)0) // expected-error {{not a constant expression}} expected-note {{reinterpret_cast}}
  };
}

namespace IndirectField {
  struct S {
    struct { // expected-warning {{GNU extension}}
      union {
        struct { // expected-warning {{GNU extension}}
          int a;
          int b;
        };
        int c;
      };
      int d;
    };
    union {
      int e;
      int f;
    };
    constexpr S(int a, int b, int d, int e) : a(a), b(b), d(d), e(e) {}
    constexpr S(int c, int d, int f) : c(c), d(d), f(f) {}
  };

  constexpr S s1(1, 2, 3, 4);
  constexpr S s2(5, 6, 7);

  // FIXME: The diagnostics here do a very poor job of explaining which unnamed
  // member is active and which is requested.
  static_assert(s1.a == 1, "");
  static_assert(s1.b == 2, "");
  static_assert(s1.c == 0, ""); // expected-error {{constant expression}} expected-note {{union with active member}}
  static_assert(s1.d == 3, "");
  static_assert(s1.e == 4, "");
  static_assert(s1.f == 0, ""); // expected-error {{constant expression}} expected-note {{union with active member}}

  static_assert(s2.a == 0, ""); // expected-error {{constant expression}} expected-note {{union with active member}}
  static_assert(s2.b == 0, ""); // expected-error {{constant expression}} expected-note {{union with active member}}
  static_assert(s2.c == 5, "");
  static_assert(s2.d == 6, "");
  static_assert(s2.e == 0, ""); // expected-error {{constant expression}} expected-note {{union with active member}}
  static_assert(s2.f == 7, "");
}

// DR1405: don't allow reading mutable members in constant expressions.
namespace MutableMembers {
  struct MM {
    mutable int n; // expected-note 3{{declared here}}
  } constexpr mm = { 4 };
  constexpr int mmn = mm.n; // expected-error {{constant expression}} expected-note {{read of mutable member 'n' is not allowed in a constant expression}}
  int x = (mm.n = 1, 3);
  constexpr int mmn2 = mm.n; // expected-error {{constant expression}} expected-note {{read of mutable member 'n' is not allowed in a constant expression}}

  // Here's one reason why allowing this would be a disaster...
  template<int n> struct Id { int k = n; };
  int f() {
    constexpr MM m = { 0 };
    ++m.n;
    return Id<m.n>().k; // expected-error {{not a constant expression}} expected-note {{read of mutable member 'n' is not allowed in a constant expression}}
  }

  struct A { int n; };
  struct B { mutable A a; }; // expected-note {{here}}
  struct C { B b; };
  constexpr C c[3] = {};
  constexpr int k = c[1].b.a.n; // expected-error {{constant expression}} expected-note {{mutable}}
}

namespace Fold {

  // This macro forces its argument to be constant-folded, even if it's not
  // otherwise a constant expression.
  #define fold(x) (__builtin_constant_p(x) ? (x) : (x))

  constexpr int n = (int)(char*)123; // expected-error {{constant expression}} expected-note {{reinterpret_cast}}
  constexpr int m = fold((int)(char*)123); // ok
  static_assert(m == 123, "");

  #undef fold

}

namespace DR1454 {

constexpr const int &f(const int &n) { return n; }
constexpr int k1 = f(0); // ok

struct Wrap {
  const int &value;
};
constexpr const Wrap &g(const Wrap &w) { return w; }
constexpr int k2 = g({0}).value; // ok

constexpr const int &i = 0; // expected-error {{constant expression}} expected-note {{temporary}} expected-note 2{{here}}
constexpr const int j = i; // expected-error {{constant expression}} expected-note {{initializer of 'i' is not a constant expression}}

}

namespace RecursiveOpaqueExpr {
  template<typename Iter>
  constexpr auto LastNonzero(Iter p, Iter q) -> decltype(+*p) {
    return p != q ? (LastNonzero(p+1, q) ?: *p) : 0; // expected-warning {{GNU}}
  }

  constexpr int arr1[] = { 1, 0, 0, 3, 0, 2, 0, 4, 0, 0 };
  static_assert(LastNonzero(begin(arr1), end(arr1)) == 4, "");

  constexpr int arr2[] = { 1, 0, 0, 3, 0, 2, 0, 4, 0, 5 };
  static_assert(LastNonzero(begin(arr2), end(arr2)) == 5, "");
}

namespace VLASizeof {

  void f(int k) {
    int arr[k]; // expected-warning {{C99}}
    constexpr int n = 1 +
        sizeof(arr) // expected-error {{constant expression}}
        * 3;
  }
}

namespace CompoundLiteral {
  // FIXME:
  // We don't model the semantics of this correctly: the compound literal is
  // represented as a prvalue in the AST, but actually behaves like an lvalue.
  // We treat the compound literal as a temporary and refuse to produce a
  // pointer to it. This is OK: we're not required to treat this as a constant
  // in C++, and in C we model compound literals as lvalues.
  constexpr int *p = (int*)(int[1]){0}; // expected-warning {{C99}} expected-error {{constant expression}} expected-note 2{{temporary}}
}

namespace Vector {
  typedef int __attribute__((vector_size(16))) VI4;
  constexpr VI4 f(int n) {
    return VI4 { n * 3, n + 4, n - 5, n / 6 };
  }
  constexpr auto v1 = f(10);

  typedef double __attribute__((vector_size(32))) VD4;
  constexpr VD4 g(int n) {
    return (VD4) { n / 2.0, n + 1.5, n - 5.4, n * 0.9 }; // expected-warning {{C99}}
  }
  constexpr auto v2 = g(4);
}
