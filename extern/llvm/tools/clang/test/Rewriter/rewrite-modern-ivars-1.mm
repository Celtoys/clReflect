// RUN: %clang_cc1 -x objective-c++ -Wno-return-type -fblocks -fms-extensions -rewrite-objc %s -o %t-rw.cpp
// RUN: %clang_cc1 -fsyntax-only -fblocks -Wno-address-of-temporary -D"Class=void*" -D"id=void*" -D"SEL=void*" -D"__declspec(X)=" %t-rw.cpp

@interface NSCheapMutableString {
@private
    struct S s0;
    union {
        char *fat;
        unsigned char *thin;
    } contents;

    struct {
        unsigned int isFat:1;
        unsigned int freeWhenDone:1;
        unsigned int refs:30;
    } flags;

    struct S {
        int iS1;
        double dS1;
    } others;

    union U {
      int iU1;
      double dU1;
    } u_others;

   enum {
    One, Two
   } E1;

   enum e {
    Yes = 1,
    No = 0
   } BoOl;

   struct S s1;

   enum e E2;

    union {
        char *fat;
        unsigned char *thin;
    } Last_contents;

    struct {
        unsigned int isFat:1;
        unsigned int freeWhenDone:1;
        unsigned int refs:30;
    } Last_flags;
}
@end

@interface III {
@private
    struct S s0;

    union {
        char *fat;
        unsigned char *thin;
    } contents;

    struct {
        unsigned int isFat:1;
        unsigned int freeWhenDone:1;
        unsigned int refs:30;
    } flags;

   enum {
    One1 = 1000, Two1, Three1
   } E1;

   struct S s1;

   enum e E2;

    union {
        char *fat;
        unsigned char *thin;
    } Last_contents;

    struct {
        unsigned int isFat:1;
        unsigned int freeWhenDone:1;
        unsigned int refs:30;
    } Last_flags;
}
@end

