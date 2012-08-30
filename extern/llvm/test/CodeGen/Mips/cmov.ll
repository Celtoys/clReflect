; RUN: llc -march=mips < %s | FileCheck %s -check-prefix=O32
; RUN: llc -march=mips -regalloc=basic < %s | FileCheck %s -check-prefix=O32
; RUN: llc -march=mips64el -mcpu=mips64 -mattr=n64 < %s | FileCheck %s -check-prefix=N64

@i1 = global [3 x i32] [i32 1, i32 2, i32 3], align 4
@i3 = common global i32* null, align 4

; O32:  lw  ${{[0-9]+}}, %got(i3)($gp)
; O32:  addiu ${{[0-9]+}}, $gp, %got(i1)
; N64:  ld  ${{[0-9]+}}, %got_disp(i3)($gp)
; N64:  daddiu ${{[0-9]+}}, $gp, %got_disp(i1)
define i32* @cmov1(i32 %s) nounwind readonly {
entry:
  %tobool = icmp ne i32 %s, 0
  %tmp1 = load i32** @i3, align 4
  %cond = select i1 %tobool, i32* getelementptr inbounds ([3 x i32]* @i1, i32 0, i32 0), i32* %tmp1
  ret i32* %cond
}

@c = global i32 1, align 4
@d = global i32 0, align 4

; O32: cmov2:
; O32: addiu $[[R1:[0-9]+]], $gp, %got(d)
; O32: addiu $[[R0:[0-9]+]], $gp, %got(c)
; O32: movn  $[[R1]], $[[R0]], ${{[0-9]+}}
; N64: cmov2:
; N64: daddiu $[[R1:[0-9]+]], $gp, %got_disp(d)
; N64: daddiu $[[R0:[0-9]+]], $gp, %got_disp(c)
; N64: movn  $[[R1]], $[[R0]], ${{[0-9]+}}
define i32 @cmov2(i32 %s) nounwind readonly {
entry:
  %tobool = icmp ne i32 %s, 0
  %tmp1 = load i32* @c, align 4
  %tmp2 = load i32* @d, align 4
  %cond = select i1 %tobool, i32 %tmp1, i32 %tmp2
  ret i32 %cond
}

