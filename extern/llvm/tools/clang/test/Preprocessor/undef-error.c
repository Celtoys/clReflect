// RUN: %clang_cc1 %s -pedantic-errors -verify 
// PR2045

#define b
/* expected-error {{extra tokens at end of #undef directive}} */ #undef a b
