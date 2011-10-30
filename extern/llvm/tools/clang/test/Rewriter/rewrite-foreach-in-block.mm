// RUN: %clang_cc1 -x objective-c++ -Wno-return-type -fblocks -fms-extensions -rewrite-objc %s -o %t-rw.cpp
// RUN: %clang_cc1 -fsyntax-only -Wno-address-of-temporary -D"id=void*" -D"SEL=void*" -D"__declspec(X)=" %t-rw.cpp
// rdar:// 9878420

void objc_enumerationMutation(id);
void *sel_registerName(const char *);
typedef void (^CoreDAVCompletionBlock)(void);

@interface I
- (void)M;
- (id) ARR;
@property (readwrite, copy, nonatomic) CoreDAVCompletionBlock c;
@end

@implementation I
- (void)M {
    I* ace;
    self.c = ^() {
          // sanity test for the changes.
	  [ace ARR];
          for (I *privilege in [ace ARR]) { }
    };
    self.c = ^() {
          // sanity test for the changes.
	  [ace ARR];
    };
}
@end
