// RUN: %clang_cc1 %s -fsyntax-only -verify
typedef const struct __CFString * CFStringRef;
#define CFSTR __builtin___CFStringMakeConstantString

void f() {
  (void)CFStringRef(CFSTR("Hello"));
}

void a() { __builtin_va_list x, y; ::__builtin_va_copy(x, y); }
