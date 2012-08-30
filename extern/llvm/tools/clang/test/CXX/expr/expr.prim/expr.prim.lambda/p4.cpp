// RUN: %clang_cc1 -fsyntax-only -std=c++11 %s -verify

void missing_lambda_declarator() {
  [](){}();
}

template<typename T> T get();

void infer_void_return_type(int i) {
  if (i > 17)
    return []() { }();

  if (i > 11)
    return []() { return; }();

  return [](int x) {
    switch (x) {
    case 0: return get<void>();
    case 1: return;
    case 2: return { 1, 2.0 }; // expected-error{{cannot deduce lambda return type from initializer list}}
    }
  }(7);
}

struct X { };

X infer_X_return_type(X x) {
  return [&x](int y) { // expected-warning{{omitted result type}}
    if (y > 0)
      return X();
    else
      return x;
  }(5);
}

X infer_X_return_type_fail(X x) { 
  return [x](int y) { // expected-warning{{omitted result type}}
    if (y > 0)
      return X();
    else 
      return x; // expected-error{{return type 'const X' must match previous return type 'X' when lambda expression has unspecified explicit return type}}
  }(5);
}

struct Incomplete; // expected-note{{forward declaration of 'Incomplete'}}
void test_result_type(int N) {
  auto l1 = [] () -> Incomplete { }; // expected-error{{incomplete result type 'Incomplete' in lambda expression}}

  typedef int vla[N];
  auto l2 = [] () -> vla { }; // expected-error{{function cannot return array type 'vla' (aka 'int [N]')}}
}
