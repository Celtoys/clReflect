@ RUN: llvm-mc < %s -triple armv7-unknown-unknown -show-encoding -mattr=+neon,+vfp4   | FileCheck %s --check-prefix=ARM
@ RUN: llvm-mc < %s -triple thumbv7-unknown-unknown -show-encoding -mattr=+neon,+vfp4 | FileCheck %s --check-prefix=THUMB

@ ARM: vfma.f64 d16, d18, d17 @ encoding: [0xa1,0x0b,0xe2,0xee]
@ THUMB: vfma.f64 d16, d18, d17 @ encoding: [0xe2,0xee,0xa1,0x0b]
vfma.f64 d16, d18, d17

@ ARM: vfma.f32 s2, s4, s0 @ encoding: [0x00,0x1a,0xa2,0xee]
@ THUMB: vfma.f32 s2, s4, s0 @ encoding: [0xa2,0xee,0x00,0x1a]
vfma.f32 s2, s4, s0

@ ARM: vfma.f32 d16, d18, d17 @ encoding: [0xb1,0x0c,0x42,0xf2]
@ THUMB: vfma.f32 d16, d18, d17 @ encoding: [0x42,0xef,0xb1,0x0c]
vfma.f32 d16, d18, d17

@ ARM: vfma.f32 q2, q4, q0 @ encoding: [0x50,0x4c,0x08,0xf2]
@ THUMB: vfma.f32	q2, q4, q0 @ encoding: [0x08,0xef,0x50,0x4c]
vfma.f32 q2, q4, q0

@ ARM: vfnma.f64 d16, d18, d17 @ encoding: [0xe1,0x0b,0xd2,0xee]
@ THUMB: vfnma.f64 d16, d18, d17 @ encoding: [0xd2,0xee,0xe1,0x0b]
vfnma.f64 d16, d18, d17

@ ARM: vfnma.f32 s2, s4, s0 @ encoding: [0x40,0x1a,0x92,0xee]
@ THUMB: vfnma.f32 s2, s4, s0 @ encoding: [0x92,0xee,0x40,0x1a]
vfnma.f32 s2, s4, s0

@ ARM: vfms.f64 d16, d18, d17 @ encoding: [0xe1,0x0b,0xe2,0xee]
@ THUMB: vfms.f64 d16, d18, d17 @ encoding: [0xe2,0xee,0xe1,0x0b]
vfms.f64 d16, d18, d17

@ ARM: vfms.f32 s2, s4, s0 @ encoding: [0x40,0x1a,0xa2,0xee]
@ THUMB: vfms.f32 s2, s4, s0 @ encoding: [0xa2,0xee,0x40,0x1a]
vfms.f32 s2, s4, s0

@ ARM: vfms.f32 d16, d18, d17 @ encoding: [0xb1,0x0c,0x62,0xf2]
@ THUMB: vfms.f32 d16, d18, d17 @ encoding: [0x62,0xef,0xb1,0x0c]
vfms.f32 d16, d18, d17

@ ARM: vfms.f32 q2, q4, q0 @ encoding: [0x50,0x4c,0x28,0xf2]
@ THUMB: vfms.f32	q2, q4, q0 @ encoding: [0x28,0xef,0x50,0x4c]
vfms.f32 q2, q4, q0

@ ARM: vfnms.f64 d16, d18, d17 @ encoding: [0xa1,0x0b,0xd2,0xee]
@ THUMB: vfnms.f64 d16, d18, d17 @ encoding: [0xd2,0xee,0xa1,0x0b]
vfnms.f64 d16, d18, d17

@ ARM: vfnms.f32 s2, s4, s0 @ encoding: [0x00,0x1a,0x92,0xee]
@ THUMB: vfnms.f32 s2, s4, s0 @ encoding: [0x92,0xee,0x00,0x1a]
vfnms.f32 s2, s4, s0
