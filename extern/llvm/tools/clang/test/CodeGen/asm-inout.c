// RUN: %clang_cc1 -triple i386-unknown-unknown -emit-llvm %s -o - | FileCheck %s
// PR3800
int *foo(void);

// CHECK: @test1
void test1() {
  // CHECK: [[REGCALLRESULT:%[a-zA-Z0-9\.]+]] = call i32* @foo()
  // CHECK: call void asm "foobar", "=*m,*m,~{dirflag},~{fpsr},~{flags}"(i32* [[REGCALLRESULT]], i32* [[REGCALLRESULT]])
  asm ("foobar" : "+m"(*foo()));
}

// CHECK: @test2
void test2() {
  // CHECK: [[REGCALLRESULT:%[a-zA-Z0-9\.]+]] = call i32* @foo()
  // CHECK: load i32* [[REGCALLRESULT]]
  // CHECK: call i32 asm
  // CHECK: store i32 {{%[a-zA-Z0-9\.]+}}, i32* [[REGCALLRESULT]]
  asm ("foobar" : "+r"(*foo()));
}

// PR7338
void test3(int *vout, int vin)
{
  // CHECK: call void asm "opr $0,$1", "=*r|m|r,r|m|r,~{edi},~{dirflag},~{fpsr},~{flags}"
asm(
		"opr %[vout],%[vin]"
		: [vout] "=r,=m,=r" (*vout)
		: [vin] "r,m,r" (vin)
		: "edi"
		);
}

// PR8959 - This should implicitly truncate the immediate to a byte.
int test4(volatile int *addr) {
  unsigned char oldval;
  __asm__ ("frob %0" : "=r"(oldval) : "0"(0xff));
  return (int)oldval;
// CHECK: call i8 asm "frob $0", "=r,0{{.*}}"(i8 -1)
}
