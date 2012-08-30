// Check passing options to the assembler for MIPS targets.
//
// RUN: %clang -target mips-linux-gnu -### \
// RUN:   -no-integrated-as -c %s 2>&1 \
// RUN:   | FileCheck -check-prefix=MIPS32-EB-AS %s
// CHECK-MIPS32-EB-AS: as{{(.exe)?}}" "-march" "mips32" "-mabi" "32" "-EB"
//
// RUN: %clang -target mipsel-linux-gnu -### \
// RUN:   -no-integrated-as -c %s 2>&1 \
// RUN:   | FileCheck -check-prefix=MIPS32-EL-AS %s
// CHECK-MIPS32-EL-AS: as{{(.exe)?}}" "-march" "mips32" "-mabi" "32" "-EL"
//
// RUN: %clang -target mips64-linux-gnu -### \
// RUN:   -no-integrated-as -c %s 2>&1 \
// RUN:   | FileCheck -check-prefix=MIPS64-EB-AS %s
// CHECK-MIPS64-EB-AS: as{{(.exe)?}}" "-march" "mips64" "-mabi" "64" "-EB"
//
// RUN: %clang -target mips64el-linux-gnu -### \
// RUN:   -no-integrated-as -c %s 2>&1 \
// RUN:   | FileCheck -check-prefix=MIPS64-EL-AS %s
// CHECK-MIPS64-EL-AS: as{{(.exe)?}}" "-march" "mips64" "-mabi" "64" "-EL"
//
// RUN: %clang -target mips-linux-gnu -mabi=eabi -### \
// RUN:   -no-integrated-as -c %s 2>&1 \
// RUN:   | FileCheck -check-prefix=MIPS-EABI %s
// CHECK-MIPS-EABI: as{{(.exe)?}}" "-march" "mips32" "-mabi" "eabi" "-EB"
//
// RUN: %clang -target mips64-linux-gnu -mabi=n32 -### \
// RUN:   -no-integrated-as -c %s 2>&1 \
// RUN:   | FileCheck -check-prefix=MIPS-N32 %s
// CHECK-MIPS-N32: as{{(.exe)?}}" "-march" "mips64" "-mabi" "n32" "-EB"
