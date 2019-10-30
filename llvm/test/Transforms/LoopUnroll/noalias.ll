; RUN: opt < %s -S -unroll-runtime -unroll-count=2 -loop-unroll | FileCheck %s

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: nounwind
define dso_local void @test_loop_unroll_01(i32* nocapture %_pA) local_unnamed_addr #0 {
entry:
  %0 = tail call i8* @llvm.noalias.decl.p0i8.p0p0i32.i32(i32** null, i32 0, metadata !2)
  %1 = tail call i32* @llvm.side.noalias.p0i32.p0i8.p0p0i32.p0p0i32.i32(i32* %_pA, i8* %0, i32** null, i32** undef, i32 0, metadata !2), !tbaa !5, !noalias !2
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body
  ret void

for.body:                                         ; preds = %for.body, %entry
  %i.06 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %arrayidx = getelementptr inbounds i32, i32* %_pA, i32 %i.06
  store i32 %i.06, i32* %arrayidx, noalias_sidechannel i32* %1, align 4, !tbaa !9, !noalias !2
  %inc = add nuw nsw i32 %i.06, 1
  %exitcond = icmp eq i32 %inc, 4
  br i1 %exitcond, label %for.cond.cleanup, label %for.body
}

; CHECK: define dso_local void @test_loop_unroll_01(i32* nocapture %_pA) local_unnamed_addr #0 {
; CHECK: entry:
; CHECK:   %0 = tail call i8* @llvm.noalias.decl.p0i8.p0p0i32.i32(i32** null, i32 0, metadata !2)
; CHECK:   %1 = tail call i32* @llvm.side.noalias.p0i32.p0i8.p0p0i32.p0p0i32.i32(i32* %_pA, i8* %0, i32** null, i32** undef, i32 0, metadata !2), !tbaa !5, !noalias !2
; CHECK:   br label %for.body
; CHECK: for.cond.cleanup:                                 ; preds = %for.body
; CHECK:   ret void
; CHECK: for.body:                                         ; preds = %for.body, %entry
; CHECK:   %i.06 = phi i32 [ 0, %entry ], [ %inc.1, %for.body ]
; CHECK:   %arrayidx = getelementptr inbounds i32, i32* %_pA, i32 %i.06
; CHECK:   store i32 %i.06, i32* %arrayidx, noalias_sidechannel i32* %1, align 4, !tbaa !9, !noalias !2
; CHECK:   %inc = add nuw nsw i32 %i.06, 1
; CHECK:   %arrayidx.1 = getelementptr inbounds i32, i32* %_pA, i32 %inc
; CHECK:   store i32 %inc, i32* %arrayidx.1, noalias_sidechannel i32* %1, align 4, !tbaa !9, !noalias !2
; CHECK:   %inc.1 = add nuw nsw i32 %inc, 1
; CHECK:   %exitcond.1 = icmp eq i32 %inc.1, 4
; CHECK:   br i1 %exitcond.1, label %for.cond.cleanup, label %for.body, !llvm.loop !11
; CHECK: }


; Function Attrs: argmemonly nounwind
declare i8* @llvm.noalias.decl.p0i8.p0p0i32.i32(i32**, i32, metadata) #1

; Function Attrs: nounwind
define dso_local void @test_loop_unroll_02(i32* nocapture %_pA) local_unnamed_addr #0 {
entry:
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body
  ret void

for.body:                                         ; preds = %for.body, %entry
  %i.06 = phi i32 [ 0, %entry ], [ %inc, %for.body ]
  %0 = tail call i8* @llvm.noalias.decl.p0i8.p0p0i32.i32(i32** null, i32 0, metadata !11)
  %1 = tail call i32* @llvm.side.noalias.p0i32.p0i8.p0p0i32.p0p0i32.i32(i32* %_pA, i8* %0, i32** null, i32** undef, i32 0, metadata !11), !tbaa !5, !noalias !11
  %arrayidx = getelementptr inbounds i32, i32* %_pA, i32 %i.06
  store i32 %i.06, i32* %arrayidx, noalias_sidechannel i32* %1, align 4, !tbaa !9, !noalias !11
  %inc = add nuw nsw i32 %i.06, 1
  %exitcond = icmp eq i32 %inc, 4
  br i1 %exitcond, label %for.cond.cleanup, label %for.body
}

; CHECK: define dso_local void @test_loop_unroll_02(i32* nocapture %_pA) local_unnamed_addr #0 {
; CHECK: entry:
; CHECK:   br label %for.body
; CHECK: for.cond.cleanup:                                 ; preds = %for.body
; CHECK:   ret void
; CHECK: for.body:                                         ; preds = %for.body, %entry
; CHECK:   %i.06 = phi i32 [ 0, %entry ], [ %inc.1, %for.body ]
; CHECK:   %0 = tail call i8* @llvm.noalias.decl.p0i8.p0p0i32.i32(i32** null, i32 0, metadata !13)
; CHECK:   %1 = tail call i32* @llvm.side.noalias.p0i32.p0i8.p0p0i32.p0p0i32.i32(i32* %_pA, i8* %0, i32** null, i32** undef, i32 0, metadata !13), !tbaa !5, !noalias !13
; CHECK:   %arrayidx = getelementptr inbounds i32, i32* %_pA, i32 %i.06
; CHECK:   store i32 %i.06, i32* %arrayidx, noalias_sidechannel i32* %1, align 4, !tbaa !9, !noalias !13
; CHECK:   %inc = add nuw nsw i32 %i.06, 1
; CHECK:   %2 = tail call i8* @llvm.noalias.decl.p0i8.p0p0i32.i32(i32** null, i32 0, metadata !16)
; CHECK:   %3 = tail call i32* @llvm.side.noalias.p0i32.p0i8.p0p0i32.p0p0i32.i32(i32* %_pA, i8* %2, i32** null, i32** undef, i32 0, metadata !16), !tbaa !5, !noalias !16
; CHECK:   %arrayidx.1 = getelementptr inbounds i32, i32* %_pA, i32 %inc
; CHECK:   store i32 %inc, i32* %arrayidx.1, noalias_sidechannel i32* %3, align 4, !tbaa !9, !noalias !16
; CHECK:   %inc.1 = add nuw nsw i32 %inc, 1
; CHECK:   %exitcond.1 = icmp eq i32 %inc.1, 4
; CHECK:   br i1 %exitcond.1, label %for.cond.cleanup, label %for.body, !llvm.loop !18
; CHECK: }


; Function Attrs: nounwind readnone speculatable
declare i32* @llvm.side.noalias.p0i32.p0i8.p0p0i32.p0p0i32.i32(i32*, i8*, i32**, i32**, i32, metadata) #2

attributes #0 = { nounwind "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { argmemonly nounwind }
attributes #2 = { nounwind readnone speculatable }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang"}
!2 = !{!3}
!3 = distinct !{!3, !4, !"test_loop_unroll_01: pA"}
!4 = distinct !{!4, !"test_loop_unroll_01"}
!5 = !{!6, !6, i64 0, i64 4}
!6 = !{!7, i64 4, !"any pointer"}
!7 = !{!8, i64 1, !"omnipotent char"}
!8 = !{!"Simple C/C++ TBAA"}
!9 = !{!10, !10, i64 0, i64 4}
!10 = !{!7, i64 4, !"int"}
!11 = !{!12}
!12 = distinct !{!12, !13, !"test_loop_unroll_02: pA"}
!13 = distinct !{!13, !"test_loop_unroll_02"}

; CHECK: !0 = !{i32 1, !"wchar_size", i32 4}
; CHECK: !1 = !{!"clang"}
; CHECK: !2 = !{!3}
; CHECK: !3 = distinct !{!3, !4, !"test_loop_unroll_01: pA"}
; CHECK: !4 = distinct !{!4, !"test_loop_unroll_01"}
; CHECK: !5 = !{!6, !6, i64 0, i64 4}
; CHECK: !6 = !{!7, i64 4, !"any pointer"}
; CHECK: !7 = !{!8, i64 1, !"omnipotent char"}
; CHECK: !8 = !{!"Simple C/C++ TBAA"}
; CHECK: !9 = !{!10, !10, i64 0, i64 4}
; CHECK: !10 = !{!7, i64 4, !"int"}
; CHECK: !11 = distinct !{!11, !12}
; CHECK: !12 = !{!"llvm.loop.unroll.disable"}
; CHECK: !13 = !{!14}
; CHECK: !14 = distinct !{!14, !15, !"test_loop_unroll_02: pA"}
; CHECK: !15 = distinct !{!15, !"test_loop_unroll_02"}
; CHECK: !16 = !{!17}
; CHECK: !17 = distinct !{!17, !15, !"test_loop_unroll_02: pA:It1"}
; CHECK: !18 = distinct !{!18, !12}
