// RUN: %clang_cc1 %s -verify -fsyntax-only

typedef int int32_t;
typedef unsigned char Boolean;

void func() {
   int32_t *vector[16];
   const char compDesc[16 + 1];
   int32_t compCount = 0;
   if (_CFCalendarDecomposeAbsoluteTimeV(compDesc, vector, compCount)) { // expected-note {{previous implicit declaration is here}} \
         expected-warning {{implicit declaration of function '_CFCalendarDecomposeAbsoluteTimeV' is invalid in C99}}
   }
}
Boolean _CFCalendarDecomposeAbsoluteTimeV(const char *componentDesc, int32_t **vector, int32_t count) { // expected-error{{conflicting types for '_CFCalendarDecomposeAbsoluteTimeV'}}
 return 0;
}

