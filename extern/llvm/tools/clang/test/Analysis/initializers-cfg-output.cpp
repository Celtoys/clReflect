// RUN: %clang_cc1 -analyze -analyzer-checker=debug.DumpCFG -cfg-add-initializers %s 2>&1 | FileCheck %s
// XPASS: *

class A {
public:
  A() {}
  A(int i) {}
};

class B : public virtual A {
public:
  B() {}
  B(int i) : A(i) {}
};

class C : public virtual A {
public:
  C() {}
  C(int i) : A(i) {}
};

class TestOrder : public C, public B, public A {
  int i;
  int& r;
public:
  TestOrder();
};

TestOrder::TestOrder()
  : r(i), B(), i(), C() {
  A a;
}

class TestControlFlow {
  int x, y, z;
public:
  TestControlFlow(bool b);
};

TestControlFlow::TestControlFlow(bool b)
  : y(b ? 0 : 1)
  , x(0)
  , z(y) {
  int v;
}

// CHECK:  [ B2 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B1
// CHECK:  [ B1 ]
// CHECK:       1: 
// CHECK:       2: A([B1.1]) (Base initializer)
// CHECK:       3: 
// CHECK:       4: C([B1.3]) (Base initializer)
// CHECK:       5: 
// CHECK:       6: B([B1.5]) (Base initializer)
// CHECK:       7: 
// CHECK:       8: A([B1.7]) (Base initializer)
// CHECK:       9: /*implicit*/int()
// CHECK:      10: i([B1.9]) (Member initializer)
// CHECK:      11: this
// CHECK:      12: [B1.11]->i
// CHECK:      13: r([B1.12]) (Member initializer)
// CHECK:      14: 
// CHECK:      15: A a;
// CHECK:     Predecessors (1): B2
// CHECK:     Successors (1): B0
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (1): B1
// CHECK:     Successors (0):
// CHECK:  [ B5 (ENTRY) ]
// CHECK:     Predecessors (0):
// CHECK:     Successors (1): B4
// CHECK:  [ B1 ]
// CHECK:       1: [B4.4] ? [B2.1] : [B3.1]
// CHECK:       2: y([B1.1]) (Member initializer)
// CHECK:       3: this
// CHECK:       4: [B1.3]->y
// CHECK:       5: [B1.4]
// CHECK:       6: z([B1.5]) (Member initializer)
// CHECK:       7: int v;
// CHECK:     Predecessors (2): B2 B3
// CHECK:     Successors (1): B0
// CHECK:  [ B2 ]
// CHECK:       1: 0
// CHECK:     Predecessors (1): B4
// CHECK:     Successors (1): B1
// CHECK:  [ B3 ]
// CHECK:       1: 1
// CHECK:     Predecessors (1): B4
// CHECK:     Successors (1): B1
// CHECK:  [ B4 ]
// CHECK:       1: 0
// CHECK:       2: x([B4.1]) (Member initializer)
// CHECK:       3: b
// CHECK:       4: [B4.3]
// CHECK:       T: [B4.4] ? ... : ...
// CHECK:     Predecessors (1): B5
// CHECK:     Successors (2): B2 B3
// CHECK:  [ B0 (EXIT) ]
// CHECK:     Predecessors (1): B1
// CHECK:     Successors (0):

