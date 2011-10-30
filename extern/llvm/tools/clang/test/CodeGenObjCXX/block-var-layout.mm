// RUN: %clang_cc1 -x objective-c++ -fblocks -fobjc-gc -triple x86_64-apple-darwin -fobjc-fragile-abi -emit-llvm %s -o %t-64.ll
// RUN: FileCheck -check-prefix LP64 --input-file=%t-64.ll %s

// See commentary in test/CodeGenObjC/block-var-layout.m, from which
// this is largely cloned.

struct S {
    int i1;
    id o1;
    struct V {
     int i2;
     id o2;
    } v1;
    int i3;
    id o3;
};

__weak id wid;
void x(id y) {}
void y(int a) {}

extern id opaque_id();

void f() {
    __block int byref_int = 0;
    char ch = 'a';
    char ch1 = 'b';
    char ch2 = 'c';
    short sh = 2;
    const id bar = (id) opaque_id();
    id baz = 0;
    __strong void *strong_void_sta;
    __block id byref_bab = (id)0;
    __block void *bl_var1;
    int i; double dob;

// Test 1
// byref int, short, char, char, char, id, id, strong void*, byref id
// 01 35 10 00
// CHECK-LP64: @"\01L_OBJC_CLASS_NAME_{{.*}}" = internal global [4 x i8] c"\015\10\00"
    void (^b)() = ^{
        byref_int = sh + ch+ch1+ch2 ;
        x(bar);
        x(baz);
        x((id)strong_void_sta);
        x(byref_bab);
    };    
    b();

// Test 2
// byref int, short, char, char, char, id, id, strong void*, byref void*, byref id
// 01 36 10 00
// CHECK-LP64: @"\01L_OBJC_CLASS_NAME_{{.*}}" = internal global [4 x i8] c"\016\10\00"
    void (^c)() = ^{
        byref_int = sh + ch+ch1+ch2 ;
        x(bar);
        x(baz);
        x((id)strong_void_sta);
        x(wid);
        bl_var1 = 0;
        x(byref_bab);
    };    
    c();

// Test 3
// byref int, short, char, char, char, id, id, byref void*, int, double, byref id
// 01 34 11 30 00
// FIXME: we'd get a better format here if we sorted by scannability, not just alignment
// CHECK-LP64: @"\01L_OBJC_CLASS_NAME_{{.*}}" = internal global [5 x i8] c"\014\11 \00"
void (^d)() = ^{
        byref_int = sh + ch+ch1+ch2 ;
        x(bar);
        x(baz);
        x(wid);
        bl_var1 = 0; 
        y(i + dob);
        x(byref_bab);
    };    
    d();

// Test4
// struct S (int, id, int, id, int, id)
// 01 41 11 11
// CHECK-LP64: @"\01L_OBJC_CLASS_NAME_{{.*}}" = internal global [5 x i8] c"\01A\11\11\00"
    struct S s2;
    void (^e)() = ^{
        x(s2.o1);
    };    
    e();
}

// Test 5 (unions/structs and their nesting):
void Test5() {
  struct S5 {
    int i1;
    id o1;
    struct V {
     int i2;
     id o2;
    } v1;
    int i3;
    union UI {
        void * i1;
        id o1;
        int i3;
        id o3;
    }ui;
  };

  union U {
        void * i1;
        id o1;
        int i3;
        id o3;
  }ui;

  struct S5 s2;
  union U u2;

// struct s2 (int, id, int, id, int, id?), union u2 (id?)
// 01 41 11 12 70 00
// CHECK-LP64: @"\01L_OBJC_CLASS_NAME_{{.*}}" = internal global [6 x i8] c"\01A\11\12p\00"
  void (^c)() = ^{
    x(s2.ui.o1);
    x(u2.o1);
  };
  c();

}

// rdar: //8417746
void CFRelease(id);
void notifyBlock(id dependentBlock) {
 id singleObservationToken;
 id token;
 void (^b)();

// id, id, void(^)()
// 01 33 00
// CHECK-LP64: @"\01L_OBJC_CLASS_NAME_{{.*}}" = internal global [3 x i8] c"\013\00"
 void (^wrapperBlock)() = ^() {
     CFRelease(singleObservationToken);
     CFRelease(singleObservationToken);
     CFRelease(token);
     CFRelease(singleObservationToken);
     b();
    };
 wrapperBlock();
}

void test_empty_block() {
// 01 00
// CHECK-LP64: @"\01L_OBJC_CLASS_NAME_{{.*}}" = internal global [2 x i8] c"\01\00"
 void (^wrapperBlock)() = ^() {
    };
 wrapperBlock();
}
