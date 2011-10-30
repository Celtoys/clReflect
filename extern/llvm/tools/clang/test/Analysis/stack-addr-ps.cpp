// RUN: %clang_cc1 -analyze -analyzer-checker=core -analyzer-store=region -verify %s

// FIXME: Only the stack-address checking in Sema catches this right now, and
// the stack analyzer doesn't handle the ImplicitCastExpr (lvalue).
const int& g() {
  int s;
  return s; // expected-warning{{Address of stack memory associated with local variable 's' returned}} expected-warning{{reference to stack memory associated with local variable 's' returned}}
}

const int& g2() {
  int s1;
  int &s2 = s1; // expected-note {{binding reference variable 's2' here}}
  return s2; // expected-warning{{Address of stack memory associated with local variable 's1' returned}} expected-warning {{reference to stack memory associated with local variable 's1' returned}}
}

const int& g3() {
  int s1;
  int &s2 = s1; // expected-note {{binding reference variable 's2' here}}
  int &s3 = s2; // expected-note {{binding reference variable 's3' here}}
  return s3; // expected-warning{{Address of stack memory associated with local variable 's1' returned}} expected-warning {{reference to stack memory associated with local variable 's1' returned}}
}

int get_value();

const int &get_reference1() { return get_value(); } // expected-warning{{Address of stack memory associated with temporary object of type 'const int' returned}} expected-warning {{returning reference to local temporary}}

const int &get_reference2() {
  const int &x = get_value(); // expected-note {{binding reference variable 'x' here}}
  return x; // expected-warning{{Address of stack memory associated with temporary object of type 'const int' returned}} expected-warning {{returning reference to local temporary}}
}

const int &get_reference3() {
  const int &x1 = get_value(); // expected-note {{binding reference variable 'x1' here}}
  const int &x2 = x1; // expected-note {{binding reference variable 'x2' here}}
  return x2; // expected-warning{{Address of stack memory associated with temporary object of type 'const int' returned}} expected-warning {{returning reference to local temporary}}
}

int global_var;
int *f1() {
  int &y = global_var;
  return &y;
}

int *f2() {
  int x1;
  int &x2 = x1; // expected-note {{binding reference variable 'x2' here}}
  return &x2; // expected-warning{{Address of stack memory associated with local variable 'x1' returned}} expected-warning {{address of stack memory associated with local variable 'x1' returned}}
}

int *f3() {
  int x1;
  int *const &x2 = &x1; // expected-note {{binding reference variable 'x2' here}}
  return x2; // expected-warning {{address of stack memory associated with local variable 'x1' returned}} expected-warning {{Address of stack memory associated with local variable 'x1' returned to caller}}
}

const int *f4() {
  const int &x1 = get_value(); // expected-note {{binding reference variable 'x1' here}}
  const int &x2 = x1; // expected-note {{binding reference variable 'x2' here}}
  return &x2; // expected-warning{{Address of stack memory associated with temporary object of type 'const int' returned}} expected-warning {{returning address of local temporary}}
}

struct S {
  int x;
};

int *mf() {
  S s1;
  S &s2 = s1; // expected-note {{binding reference variable 's2' here}}
  int &x = s2.x; // expected-note {{binding reference variable 'x' here}}
  return &x; // expected-warning{{Address of stack memory associated with local variable 's1' returned}} expected-warning {{address of stack memory associated with local variable 's1' returned}}
}

void *lf() {
    label:
    void *const &x = &&label; // expected-note {{binding reference variable 'x' here}}
    return x; // expected-warning {{returning address of label, which is local}}
}

template <typename T>
struct TS {
  int *get();
  int *m() {
    int *&x = get();
    return x;
  }
};
