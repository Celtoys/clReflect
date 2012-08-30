; RUN: llc < %s -mtriple=x86_64-apple-darwin -mcpu=corei7-avx -mattr=+avx | FileCheck %s

; CHECK: @A
; CHECK-NOT: vunpck
; CHECK: vextractf128 $1
define <8 x float> @A(<8 x float> %a) nounwind uwtable readnone ssp {
entry:
  %shuffle = shufflevector <8 x float> %a, <8 x float> undef, <8 x i32> <i32 4, i32 5, i32 6, i32 7, i32 8, i32 8, i32 8, i32 8>
  ret <8 x float> %shuffle
}

; CHECK: @B
; CHECK-NOT: vunpck
; CHECK: vextractf128 $1
define <4 x double> @B(<4 x double> %a) nounwind uwtable readnone ssp {
entry:
  %shuffle = shufflevector <4 x double> %a, <4 x double> undef, <4 x i32> <i32 2, i32 3, i32 4, i32 4>
  ret <4 x double> %shuffle
}

; CHECK: @t0
; CHECK-NOT: vextractf128 $0, %ymm0, %xmm0
; CHECK-NOT: vmovaps %xmm0, (%rdi)
; CHECK: vextractf128 $0, %ymm0, (%rdi)
define void @t0(float* nocapture %addr, <8 x float> %a) nounwind uwtable ssp {
entry:
  %0 = tail call <4 x float> @llvm.x86.avx.vextractf128.ps.256(<8 x float> %a, i8 0)
  %1 = bitcast float* %addr to <4 x float>*
  store <4 x float> %0, <4 x float>* %1, align 16
  ret void
}

declare <4 x float> @llvm.x86.avx.vextractf128.ps.256(<8 x float>, i8) nounwind readnone

; CHECK: @t1
; CHECK-NOT: vextractf128 $0, %ymm0, %xmm0
; CHECK-NOT: vmovups %xmm0, (%rdi)
; CHECK: vextractf128 $0, %ymm0, (%rdi)
define void @t1(float* %addr, <8 x float> %a) nounwind uwtable ssp {
entry:
  %0 = tail call <4 x float> @llvm.x86.avx.vextractf128.ps.256(<8 x float> %a, i8 0)
  %1 = bitcast float* %addr to i8*
  tail call void @llvm.x86.sse.storeu.ps(i8* %1, <4 x float> %0)
  ret void
}

declare void @llvm.x86.sse.storeu.ps(i8*, <4 x float>) nounwind

; CHECK: @t2
; CHECK-NOT: vextractf128 $0, %ymm0, %xmm0
; CHECK-NOT: vmovaps %xmm0, (%rdi)
; CHECK: vextractf128 $0, %ymm0, (%rdi)
define void @t2(double* nocapture %addr, <4 x double> %a) nounwind uwtable ssp {
entry:
  %0 = tail call <2 x double> @llvm.x86.avx.vextractf128.pd.256(<4 x double> %a, i8 0)
  %1 = bitcast double* %addr to <2 x double>*
  store <2 x double> %0, <2 x double>* %1, align 16
  ret void
}

declare <2 x double> @llvm.x86.avx.vextractf128.pd.256(<4 x double>, i8) nounwind readnone

; CHECK: @t3
; CHECK-NOT: vextractf128 $0, %ymm0, %xmm0
; CHECK-NOT: vmovups %xmm0, (%rdi)
; CHECK: vextractf128 $0, %ymm0, (%rdi)
define void @t3(double* %addr, <4 x double> %a) nounwind uwtable ssp {
entry:
  %0 = tail call <2 x double> @llvm.x86.avx.vextractf128.pd.256(<4 x double> %a, i8 0)
  %1 = bitcast double* %addr to i8*
  tail call void @llvm.x86.sse2.storeu.pd(i8* %1, <2 x double> %0)
  ret void
}

declare void @llvm.x86.sse2.storeu.pd(i8*, <2 x double>) nounwind

; CHECK: @t4
; CHECK-NOT: vextractf128 $0, %ymm0, %xmm0
; CHECK-NOT: vmovaps %xmm0, (%rdi)
; CHECK: vextractf128 $0, %ymm0, (%rdi)
define void @t4(<2 x i64>* nocapture %addr, <4 x i64> %a) nounwind uwtable ssp {
entry:
  %0 = bitcast <4 x i64> %a to <8 x i32>
  %1 = tail call <4 x i32> @llvm.x86.avx.vextractf128.si.256(<8 x i32> %0, i8 0)
  %2 = bitcast <4 x i32> %1 to <2 x i64>
  store <2 x i64> %2, <2 x i64>* %addr, align 16
  ret void
}

declare <4 x i32> @llvm.x86.avx.vextractf128.si.256(<8 x i32>, i8) nounwind readnone

; CHECK: @t5
; CHECK-NOT: vextractf128 $0, %ymm0, %xmm0
; CHECK-NOT: vmovdqu %xmm0, (%rdi)
; CHECK: vextractf128 $0, %ymm0, (%rdi)
define void @t5(<2 x i64>* %addr, <4 x i64> %a) nounwind uwtable ssp {
entry:
  %0 = bitcast <4 x i64> %a to <8 x i32>
  %1 = tail call <4 x i32> @llvm.x86.avx.vextractf128.si.256(<8 x i32> %0, i8 0)
  %2 = bitcast <2 x i64>* %addr to i8*
  %3 = bitcast <4 x i32> %1 to <16 x i8>
  tail call void @llvm.x86.sse2.storeu.dq(i8* %2, <16 x i8> %3)
  ret void
}

declare void @llvm.x86.sse2.storeu.dq(i8*, <16 x i8>) nounwind
