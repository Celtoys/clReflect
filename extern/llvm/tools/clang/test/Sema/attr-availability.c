// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fsyntax-only -verify %s

void f0() __attribute__((availability(macosx,introduced=10.4,deprecated=10.2))); // expected-warning{{feature cannot be deprecated in Mac OS X version 10.2 before it was introduced in version 10.4; attribute ignored}}
void f1() __attribute__((availability(ios,obsoleted=2.1,deprecated=3.0)));  // expected-warning{{feature cannot be obsoleted in iOS version 2.1 before it was deprecated in version 3.0; attribute ignored}}
void f2() __attribute__((availability(ios,introduced=2.1,deprecated=2.1)));

void f3() __attribute__((availability(otheros,introduced=2.2))); // expected-warning{{unknown platform 'otheros' in availability macro}}

// rdar://10095131
extern void 
ATSFontGetName(const char *oName) __attribute__((availability(macosx,introduced=8.0,deprecated=9.0, message="use CTFontCopyFullName")));

extern void
ATSFontGetPostScriptName(int flags) __attribute__((availability(macosx,introduced=8.0,obsoleted=9.0, message="use ATSFontGetFullPostScriptName"))); // expected-note {{function has been explicitly marked unavailable here}}

void test_10095131() {
  ATSFontGetName("Hello"); // expected-warning {{'ATSFontGetName' is deprecated: first deprecated in Mac OS X 9.0 - use CTFontCopyFullName}}
  ATSFontGetPostScriptName(100); // expected-error {{'ATSFontGetPostScriptName' is unavailable: obsoleted in Mac OS X 9.0 - use ATSFontGetFullPostScriptName}}
}

// rdar://10711037
__attribute__((availability(macos, unavailable))) // expected-warning {{attribute 'availability' is ignored}}
enum {
    NSDataWritingFileProtectionWriteOnly = 0x30000000,
    NSDataWritingFileProtectionCompleteUntilUserAuthentication = 0x40000000,
};
