// RUN: %clang_cc1 -fsyntax-only -detailed-preprocessing-record %s

// http://llvm.org/PR11120

#define STRINGIZE(text) STRINGIZE_I(text)
#define STRINGIZE_I(text) #text

#define INC pp-record.h

#include STRINGIZE(INC)

CAKE;
