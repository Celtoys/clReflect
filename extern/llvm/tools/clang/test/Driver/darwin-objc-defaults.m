// Check non-fragile ABI and dispatch method defaults.

// i386

// RUN: %clang -target x86_64-apple-darwin10 -S -### %s \
// RUN:   -arch i386 -mmacosx-version-min=10.5 2> %t
// RUN: FileCheck --check-prefix CHECK-I386_OSX10_5 < %t %s

// CHECK-CHECK-I386_OSX10_5: "-cc1"
// CHECK-CHECK-I386_OSX10_5: -fobjc-fragile-abi
// CHECK-CHECK-I386_OSX10_5-NOT: -fobjc-dispatch-method
// CHECK-CHECK-I386_OSX10_5: darwin-objc-defaults

// RUN: %clang -target x86_64-apple-darwin10 -S -### %s \
// RUN:   -arch i386 -mmacosx-version-min=10.6 2> %t
// RUN: FileCheck --check-prefix CHECK-I386_OSX10_6 < %t %s

// CHECK-CHECK-I386_OSX10_6: "-cc1"
// CHECK-CHECK-I386_OSX10_6: -fobjc-fragile-abi
// CHECK-CHECK-I386_OSX10_6-NOT: -fobjc-dispatch-method
// CHECK-CHECK-I386_OSX10_6: darwin-objc-defaults

// RUN: %clang -target x86_64-apple-darwin10 -S -### %s \
// RUN:   -arch i386 -miphoneos-version-min=3.0 2> %t
// RUN: FileCheck --check-prefix CHECK-I386_IPHONE3_0 < %t %s

// CHECK-CHECK-I386_IPHONE3_0: "-cc1"
// CHECK-CHECK-I386_IPHONE3_0: -fobjc-fragile-abi
// CHECK-CHECK-I386_IPHONE3_0-NOT: -fobjc-dispatch-method
// CHECK-CHECK-I386_IPHONE3_0: darwin-objc-defaults

// x86_64

// RUN: %clang -target x86_64-apple-darwin10 -S -### %s \
// RUN:   -arch x86_64 -mmacosx-version-min=10.5 2> %t
// RUN: FileCheck --check-prefix CHECK-X86_64_OSX10_5 < %t %s

// CHECK-CHECK-X86_64_OSX10_5: "-cc1"
// CHECK-CHECK-X86_64_OSX10_5-NOT: -fobjc-fragile-abi
// CHECK-CHECK-X86_64_OSX10_5: -fobjc-dispatch-method=non-legacy
// CHECK-CHECK-X86_64_OSX10_5: darwin-objc-defaults

// RUN: %clang -target x86_64-apple-darwin10 -S -### %s \
// RUN:   -arch x86_64 -mmacosx-version-min=10.6 2> %t
// RUN: FileCheck --check-prefix CHECK-X86_64_OSX10_6 < %t %s

// CHECK-CHECK-X86_64_OSX10_6: "-cc1"
// CHECK-CHECK-X86_64_OSX10_6-NOT: -fobjc-fragile-abi
// CHECK-CHECK-X86_64_OSX10_6: -fobjc-dispatch-method=mixed
// CHECK-CHECK-X86_64_OSX10_6: darwin-objc-defaults

// RUN: %clang -target x86_64-apple-darwin10 -S -### %s \
// RUN:   -arch x86_64 -miphoneos-version-min=3.0 2> %t
// RUN: FileCheck --check-prefix CHECK-X86_64_IPHONE3_0 < %t %s

// CHECK-CHECK-X86_64_IPHONE3_0: "-cc1"
// CHECK-CHECK-X86_64_IPHONE3_0-NOT: -fobjc-fragile-abi
// CHECK-CHECK-X86_64_IPHONE3_0: -fobjc-dispatch-method=mixed
// CHECK-CHECK-X86_64_IPHONE3_0: darwin-objc-defaults

// armv7

// RUN: %clang -target x86_64-apple-darwin10 -S -### %s \
// RUN:   -arch armv7 -mmacosx-version-min=10.5 2> %t
// RUN: FileCheck --check-prefix CHECK-ARMV7_OSX10_5 < %t %s

// CHECK-CHECK-ARMV7_OSX10_5: "-cc1"
// CHECK-CHECK-ARMV7_OSX10_5-NOT: -fobjc-fragile-abi
// CHECK-CHECK-ARMV7_OSX10_5-NOT: -fobjc-dispatch-method
// CHECK-CHECK-ARMV7_OSX10_5: darwin-objc-defaults

// RUN: %clang -target x86_64-apple-darwin10 -S -### %s	\
// RUN:   -arch armv7 -mmacosx-version-min=10.6 2> %t
// RUN: FileCheck --check-prefix CHECK-ARMV7_OSX10_6 < %t %s

// CHECK-CHECK-ARMV7_OSX10_6: "-cc1"
// CHECK-CHECK-ARMV7_OSX10_6-NOT: -fobjc-fragile-abi
// CHECK-CHECK-ARMV7_OSX10_6-NOT: -fobjc-dispatch-method
// CHECK-CHECK-ARMV7_OSX10_6: darwin-objc-defaults

// RUN: %clang -target x86_64-apple-darwin10 -S -### %s \
// RUN:   -arch armv7 -miphoneos-version-min=3.0 2> %t
// RUN: FileCheck --check-prefix CHECK-ARMV7_IPHONE3_0 < %t %s

// CHECK-CHECK-ARMV7_IPHONE3_0: "-cc1"
// CHECK-CHECK-ARMV7_IPHONE3_0-NOT: -fobjc-fragile-abi
// CHECK-CHECK-ARMV7_IPHONE3_0-NOT: -fobjc-dispatch-method
// CHECK-CHECK-ARMV7_IPHONE3_0: darwin-objc-defaults
