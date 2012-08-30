// RUN: %clang -target mips64el-unknown-linux -ccc-clang-archs mips64el -O3 -S -mabi=n64 -o - -emit-llvm %s | FileCheck %s

class B0 {
  double d;
};

class D0 : public B0 {
  float f;
};

class B1 {
};

class D1 : public B1 {
  double d;
  float f;
};

class D2 : public B0 {
  double d2;
};

extern D0 gd0;
extern D1 gd1;
extern D2 gd2;

// CHECK: define { i64, i64 } @_Z4foo1v() 
D0 foo1(void) {
  return gd0;
}

// CHECK: define { double, float } @_Z4foo2v() 
D1 foo2(void) {
  return gd1;
}

// CHECK: define void @_Z4foo32D2(i64 %a0.coerce0, double %a0.coerce1) 
void foo3(D2 a0) {
  gd2 = a0;
}

// CHECK: define void @_Z4foo42D0(%class.D0* nocapture byval %a0)
void foo4(D0 a0) {
  gd0 = a0;
}

