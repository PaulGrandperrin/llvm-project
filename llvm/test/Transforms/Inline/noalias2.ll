; RUN: opt -inline -enable-noalias-to-md-conversion -use-noalias-intrinsic-during-inlining=0 -S < %s | FileCheck %s -check-prefix=MD-SCOPE
; RUN: opt -inline -enable-noalias-to-md-conversion -use-noalias-intrinsic-during-inlining=1 -S < %s | FileCheck %s -check-prefix=INTR-SCOPE

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @hello(float* noalias nocapture %a, float* noalias nocapture readonly %c) #0 {
entry:
  %0 = load float, float* %c, align 4
  %arrayidx = getelementptr inbounds float, float* %a, i64 5
  store float %0, float* %arrayidx, align 4
  ret void
}

define void @foo(float* noalias nocapture %a, float* noalias nocapture readonly %c) #0 {
entry:
  tail call void @hello(float* %a, float* %c)
  %0 = load float, float* %c, align 4
  %arrayidx = getelementptr inbounds float, float* %a, i64 7
  store float %0, float* %arrayidx, align 4
  ret void
}

; MD-SCOPE: define void @foo(float* noalias nocapture %a, float* noalias nocapture readonly %c) #0 {
; MD-SCOPE: entry:
; MD-SCOPE:   %0 = load float, float* %c, align 4, !alias.scope !0, !noalias !3
; MD-SCOPE:   %arrayidx.i = getelementptr inbounds float, float* %a, i64 5
; MD-SCOPE:   store float %0, float* %arrayidx.i, align 4, !alias.scope !3, !noalias !0
; MD-SCOPE:   %1 = load float, float* %c, align 4
; MD-SCOPE:   %arrayidx = getelementptr inbounds float, float* %a, i64 7
; MD-SCOPE:   store float %1, float* %arrayidx, align 4
; MD-SCOPE:   ret void
; MD-SCOPE: }

; INTR-SCOPE: define void @foo(float* noalias nocapture %a, float* noalias nocapture readonly %c) #0 {
; INTR-SCOPE: entry:
; INTR-SCOPE:   %0 = call i8* @llvm.noalias.decl.p0i8.p0p0f32.i64(float** null, i64 0, metadata !0)
; INTR-SCOPE:   %1 = call float* @llvm.noalias.p0f32.p0i8.p0p0f32.i64(float* %a, i8* %0, float** null, i64 0, metadata !0)
; INTR-SCOPE:   %2 = call i8* @llvm.noalias.decl.p0i8.p0p0f32.i64(float** null, i64 0, metadata !3)
; INTR-SCOPE:   %3 = call float* @llvm.noalias.p0f32.p0i8.p0p0f32.i64(float* %c, i8* %2, float** null, i64 0, metadata !3)
; INTR-SCOPE:   %4 = load float, float* %3, align 4, !noalias !5
; INTR-SCOPE:   %arrayidx.i = getelementptr inbounds float, float* %1, i64 5
; INTR-SCOPE:   store float %4, float* %arrayidx.i, align 4, !noalias !5
; INTR-SCOPE:   %5 = load float, float* %c, align 4
; INTR-SCOPE:   %arrayidx = getelementptr inbounds float, float* %a, i64 7
; INTR-SCOPE:   store float %5, float* %arrayidx, align 4
; INTR-SCOPE:   ret void
; INTR-SCOPE: }

define void @hello2(float* noalias nocapture %a, float* noalias nocapture %b, float* nocapture readonly %c) #0 {
entry:
  %0 = load float, float* %c, align 4
  %arrayidx = getelementptr inbounds float, float* %a, i64 6
  store float %0, float* %arrayidx, align 4
  %arrayidx1 = getelementptr inbounds float, float* %b, i64 8
  store float %0, float* %arrayidx1, align 4
  ret void
}

; Check that when hello() is inlined into foo(), and then foo() is inlined into
; foo2(), the noalias scopes are properly concatenated.
define void @foo2(float* nocapture %a, float* nocapture %b, float* nocapture readonly %c) #0 {
entry:
  tail call void @foo(float* %a, float* %c)
  tail call void @hello2(float* %a, float* %b, float* %c)
  %0 = load float, float* %c, align 4
  %arrayidx = getelementptr inbounds float, float* %a, i64 7
  store float %0, float* %arrayidx, align 4
  ret void
}

; MD-SCOPE: define void @foo2(float* nocapture %a, float* nocapture %b, float* nocapture readonly %c) #0 {
; MD-SCOPE: entry:
; MD-SCOPE:   %0 = load float, float* %c, align 4, !alias.scope !5, !noalias !10
; MD-SCOPE:   %arrayidx.i.i = getelementptr inbounds float, float* %a, i64 5
; MD-SCOPE:   store float %0, float* %arrayidx.i.i, align 4, !alias.scope !10, !noalias !5
; MD-SCOPE:   %1 = load float, float* %c, align 4, !alias.scope !13, !noalias !14
; MD-SCOPE:   %arrayidx.i = getelementptr inbounds float, float* %a, i64 7
; MD-SCOPE:   store float %1, float* %arrayidx.i, align 4, !alias.scope !14, !noalias !13
; MD-SCOPE:   %2 = load float, float* %c, align 4, !noalias !15
; MD-SCOPE:   %arrayidx.i1 = getelementptr inbounds float, float* %a, i64 6
; MD-SCOPE:   store float %2, float* %arrayidx.i1, align 4, !alias.scope !19, !noalias !20
; MD-SCOPE:   %arrayidx1.i = getelementptr inbounds float, float* %b, i64 8
; MD-SCOPE:   store float %2, float* %arrayidx1.i, align 4, !alias.scope !20, !noalias !19
; MD-SCOPE:   %3 = load float, float* %c, align 4
; MD-SCOPE:   %arrayidx = getelementptr inbounds float, float* %a, i64 7
; MD-SCOPE:   store float %3, float* %arrayidx, align 4
; MD-SCOPE:   ret void
; MD-SCOPE: }

; INTR-SCOPE: define void @foo2(float* nocapture %a, float* nocapture %b, float* nocapture readonly %c) #0 {
; INTR-SCOPE: entry:
; INTR-SCOPE:   %0 = call i8* @llvm.noalias.decl.p0i8.p0p0f32.i64(float** null, i64 0, metadata !6)
; INTR-SCOPE:   %1 = call float* @llvm.noalias.p0f32.p0i8.p0p0f32.i64(float* %a, i8* %0, float** null, i64 0, metadata !6)
; INTR-SCOPE:   %2 = call i8* @llvm.noalias.decl.p0i8.p0p0f32.i64(float** null, i64 0, metadata !9)
; INTR-SCOPE:   %3 = call float* @llvm.noalias.p0f32.p0i8.p0p0f32.i64(float* %c, i8* %2, float** null, i64 0, metadata !9)
; INTR-SCOPE:   %4 = call i8* @llvm.noalias.decl.p0i8.p0p0f32.i64(float** null, i64 0, metadata !11) #3, !noalias !14
; INTR-SCOPE:   %5 = call float* @llvm.noalias.p0f32.p0i8.p0p0f32.i64(float* %1, i8* %4, float** null, i64 0, metadata !11) #3, !noalias !14
; INTR-SCOPE:   %6 = call i8* @llvm.noalias.decl.p0i8.p0p0f32.i64(float** null, i64 0, metadata !15) #3, !noalias !14
; INTR-SCOPE:   %7 = call float* @llvm.noalias.p0f32.p0i8.p0p0f32.i64(float* %3, i8* %6, float** null, i64 0, metadata !15) #3, !noalias !14
; INTR-SCOPE:   %8 = load float, float* %7, align 4, !noalias !17
; INTR-SCOPE:   %arrayidx.i.i = getelementptr inbounds float, float* %5, i64 5
; INTR-SCOPE:   store float %8, float* %arrayidx.i.i, align 4, !noalias !17
; INTR-SCOPE:   %9 = load float, float* %3, align 4, !noalias !14
; INTR-SCOPE:   %arrayidx.i = getelementptr inbounds float, float* %1, i64 7
; INTR-SCOPE:   store float %9, float* %arrayidx.i, align 4, !noalias !14
; INTR-SCOPE:   %10 = call i8* @llvm.noalias.decl.p0i8.p0p0f32.i64(float** null, i64 0, metadata !18)
; INTR-SCOPE:   %11 = call float* @llvm.noalias.p0f32.p0i8.p0p0f32.i64(float* %a, i8* %10, float** null, i64 0, metadata !18)
; INTR-SCOPE:   %12 = call i8* @llvm.noalias.decl.p0i8.p0p0f32.i64(float** null, i64 0, metadata !21)
; INTR-SCOPE:   %13 = call float* @llvm.noalias.p0f32.p0i8.p0p0f32.i64(float* %b, i8* %12, float** null, i64 0, metadata !21)
; INTR-SCOPE:   %14 = load float, float* %c, align 4, !noalias !23
; INTR-SCOPE:   %arrayidx.i1 = getelementptr inbounds float, float* %11, i64 6
; INTR-SCOPE:   store float %14, float* %arrayidx.i1, align 4, !noalias !23
; INTR-SCOPE:   %arrayidx1.i = getelementptr inbounds float, float* %13, i64 8
; INTR-SCOPE:   store float %14, float* %arrayidx1.i, align 4, !noalias !23
; INTR-SCOPE:   %15 = load float, float* %c, align 4
; INTR-SCOPE:   %arrayidx = getelementptr inbounds float, float* %a, i64 7
; INTR-SCOPE:   store float %15, float* %arrayidx, align 4
; INTR-SCOPE:   ret void
; INTR-SCOPE: }

; MD-SCOPE: !0 = !{!1}
; MD-SCOPE: !1 = distinct !{!1, !2, !"hello: %c"}
; MD-SCOPE: !2 = distinct !{!2, !"hello"}
; MD-SCOPE: !3 = !{!4}
; MD-SCOPE: !4 = distinct !{!4, !2, !"hello: %a"}
; MD-SCOPE: !5 = !{!6, !8}
; MD-SCOPE: !6 = distinct !{!6, !7, !"hello: %c"}
; MD-SCOPE: !7 = distinct !{!7, !"hello"}
; MD-SCOPE: !8 = distinct !{!8, !9, !"foo: %c"}
; MD-SCOPE: !9 = distinct !{!9, !"foo"}
; MD-SCOPE: !10 = !{!11, !12}
; MD-SCOPE: !11 = distinct !{!11, !7, !"hello: %a"}
; MD-SCOPE: !12 = distinct !{!12, !9, !"foo: %a"}
; MD-SCOPE: !13 = !{!8}
; MD-SCOPE: !14 = !{!12}
; MD-SCOPE: !15 = !{!16, !18}
; MD-SCOPE: !16 = distinct !{!16, !17, !"hello2: %a"}
; MD-SCOPE: !17 = distinct !{!17, !"hello2"}
; MD-SCOPE: !18 = distinct !{!18, !17, !"hello2: %b"}
; MD-SCOPE: !19 = !{!16}
; MD-SCOPE: !20 = !{!18}

; INTR-SCOPE: !0 = !{!1}
; INTR-SCOPE: !1 = distinct !{!1, !2, !"hello: %a"}
; INTR-SCOPE: !2 = distinct !{!2, !"hello"}
; INTR-SCOPE: !3 = !{!4}
; INTR-SCOPE: !4 = distinct !{!4, !2, !"hello: %c"}
; INTR-SCOPE: !5 = !{!1, !4}
; INTR-SCOPE: !6 = !{!7}
; INTR-SCOPE: !7 = distinct !{!7, !8, !"foo: %a"}
; INTR-SCOPE: !8 = distinct !{!8, !"foo"}
; INTR-SCOPE: !9 = !{!10}
; INTR-SCOPE: !10 = distinct !{!10, !8, !"foo: %c"}
; INTR-SCOPE: !11 = !{!12}
; INTR-SCOPE: !12 = distinct !{!12, !13, !"hello: %a"}
; INTR-SCOPE: !13 = distinct !{!13, !"hello"}
; INTR-SCOPE: !14 = !{!7, !10}
; INTR-SCOPE: !15 = !{!16}
; INTR-SCOPE: !16 = distinct !{!16, !13, !"hello: %c"}
; INTR-SCOPE: !17 = !{!12, !16, !7, !10}
; INTR-SCOPE: !18 = !{!19}
; INTR-SCOPE: !19 = distinct !{!19, !20, !"hello2: %a"}
; INTR-SCOPE: !20 = distinct !{!20, !"hello2"}
; INTR-SCOPE: !21 = !{!22}
; INTR-SCOPE: !22 = distinct !{!22, !20, !"hello2: %b"}
; INTR-SCOPE: !23 = !{!19, !22}

attributes #0 = { nounwind uwtable }
