// RUN: %clang_cc1 -triple i386-unknown-unknown -emit-llvm %s -o - | FileCheck -check-prefix=C %s
// RUN: %clang_cc1 -x c++ -triple i386-unknown-unknown -emit-llvm %s -o - | FileCheck -check-prefix=C %s
// RUN: %clang_cc1 -x c++ -std=c++11 -triple i386-unknown-unknown -emit-llvm %s -o - | FileCheck -check-prefix=CPP0X %s

#include <stddef.h>

int main() {
  // CHECK-C: private unnamed_addr constant [10 x i8] c"abc\00\00\00\00\00\00\00", align 1
  // CHECK-CPP0X: private unnamed_addr constant [10 x i8] c"abc\00\00\00\00\00\00\00", align 1
  char a[10] = "abc";

  // This should convert to utf8.
  // CHECK-C: private unnamed_addr constant [10 x i8] c"\E1\84\A0\C8\A0\F4\82\80\B0\00", align 1
  // CHECK-CPP0X: private unnamed_addr constant [10 x i8] c"\E1\84\A0\C8\A0\F4\82\80\B0\00", align 1
  char b[10] = "\u1120\u0220\U00102030";

  // CHECK-C: private unnamed_addr constant [12 x i8] c"A\00\00\00B\00\00\00\00\00\00\00", align 4
  // CHECK-CPP0X: private unnamed_addr constant [12 x i8] c"A\00\00\00B\00\00\00\00\00\00\00", align 4
  const wchar_t *foo = L"AB";

  // CHECK-C: private unnamed_addr constant [12 x i8] c"4\12\00\00\0B\F0\10\00\00\00\00\00", align 4
  // CHECK-CPP0X: private unnamed_addr constant [12 x i8] c"4\12\00\00\0B\F0\10\00\00\00\00\00", align 4
  const wchar_t *bar = L"\u1234\U0010F00B";

  // CHECK-C: private unnamed_addr constant [12 x i8] c"4\12\00\00\0C\F0\10\00\00\00\00\00", align 4
  // CHECK-CPP0X: private unnamed_addr constant [12 x i8] c"4\12\00\00\0C\F0\10\00\00\00\00\00", align 4
  const wchar_t *baz = L"\u1234" "\U0010F00C";

#if __cplusplus >= 201103L
  // CHECK-CPP0X: private unnamed_addr constant [12 x i8] c"C\00\00\00D\00\00\00\00\00\00\00", align 4
  const char32_t *c = U"CD";

  // CHECK-CPP0X: private unnamed_addr constant [12 x i8] c"5\12\00\00\0C\F0\10\00\00\00\00\00", align 4
  const char32_t *d = U"\u1235\U0010F00C";

  // CHECK-CPP0X: private unnamed_addr constant [12 x i8] c"5\12\00\00\0B\F0\10\00\00\00\00\00", align 4
  const char32_t *o = "\u1235" U"\U0010F00B";

  // CHECK-CPP0X: private unnamed_addr constant [6 x i8] c"E\00F\00\00\00", align 2
  const char16_t *e = u"EF";

  // This should convert to utf16.
  // CHECK-CPP0X: private unnamed_addr constant [10 x i8] c" \11 \02\C8\DB0\DC\00\00", align 2
  const char16_t *f = u"\u1120\u0220\U00102030";

  // This should convert to utf16.
  // CHECK-CPP0X: private unnamed_addr constant [10 x i8] c" \11 \03\C8\DB0\DC\00\00", align 2
  const char16_t *p = u"\u1120\u0320" "\U00102030";

  // CHECK-CPP0X: private unnamed_addr constant [4 x i8] c"def\00", align 1
  const char *g = u8"def";

  // CHECK-CPP0X: private unnamed_addr constant [4 x i8] c"ghi\00", align 1
  const char *h = R"foo(ghi)foo";

  // CHECK-CPP0X: private unnamed_addr constant [4 x i8] c"jkl\00", align 1
  const char *i = u8R"bar(jkl)bar";

  // CHECK-CPP0X: private unnamed_addr constant [6 x i8] c"G\00H\00\00\00", align 2
  const char16_t *j = uR"foo(GH)foo";

  // CHECK-CPP0X: private unnamed_addr constant [12 x i8] c"I\00\00\00J\00\00\00\00\00\00\00", align 4
  const char32_t *k = UR"bar(IJ)bar";

  // CHECK-CPP0X: private unnamed_addr constant [12 x i8] c"K\00\00\00L\00\00\00\00\00\00\00", align 4
  const wchar_t *l = LR"bar(KL)bar";

  // CHECK-CPP0X: private unnamed_addr constant [9 x i8] c"abc\5Cndef\00", align 1
  const char *m = R"(abc\ndef)";

  // CHECK-CPP0X: private unnamed_addr constant [8 x i8] c"abc\0Adef\00", align 1
  const char *n = R"(abc
def)";

  // CHECK-CPP0X: private unnamed_addr constant [11 x i8] c"abc\0Adefghi\00", align 1
  const char *q = R"(abc
def)" "ghi";

#endif
}
