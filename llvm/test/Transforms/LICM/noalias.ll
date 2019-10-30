; RUN: opt -S -basicaa -licm -enable-mssa-loop-dependency=false %s | FileCheck -check-prefixes=CHECK,AST %s
; RUN: opt -S -basicaa -licm -enable-mssa-loop-dependency=true %s | FileCheck  -check-prefixes=CHECK,MSSA %s
; RUN: opt -aa-pipeline=basic-aa -passes='require<aa>,require<targetir>,require<scalar-evolution>,require<opt-remark-emit>,loop(licm)' < %s -S | FileCheck -check-prefixes=CHECK,AST %s


; Function Attrs: nounwind
define dso_local void @test01(i32* %_p, i32 %n) #0 {
entry:
  %0 = call i8* @llvm.noalias.decl.p0i8.p0p0i32.i32(i32** null, i32 0, metadata !2)
  br label %do.body

do.body:                                          ; preds = %do.body, %entry
  %n.addr.0 = phi i32 [ %n, %entry ], [ %dec, %do.body ]
  %1 = call i32* @llvm.side.noalias.p0i32.p0i8.p0p0i32.p0p0i32.i32(i32* %_p, i8* %0, i32** null, i32** undef, i32 0, metadata !2), !tbaa !5, !noalias !2
  store i32 42, i32* %_p, noalias_sidechannel i32* %1, align 4, !tbaa !9, !noalias !2
  %dec = add nsw i32 %n.addr.0, -1
  %cmp = icmp ne i32 %dec, 0
  br i1 %cmp, label %do.body, label %do.end

do.end:                                           ; preds = %do.body
  ret void
}

; CHECK-LABEL: @test01(
; CHECK-LABEL: entry:
; CHECK:  %0 = call i8* @llvm.noalias.decl.p0i8.p0p0i32.i32(i32** null, i32 0, metadata !2)
; CHECK:  %1 = call i32* @llvm.side.noalias.p0i32.p0i8.p0p0i32.p0p0i32.i32(i32* %_p, i8* %0, i32** null, i32** undef, i32 0, metadata !2), !tbaa !5, !noalias !2
; MSSA: store i32 42, i32* %_p, noalias_sidechannel i32* %1, align 4, !tbaa !9, !noalias !2
; CHECK-LABEL: do.body:
; CHECK-LABEL: do.end:
; AST: store i32 42, i32* %_p, align 4, !tbaa !9
; CHECK: ret void

; Function Attrs: nounwind
define dso_local void @test02(i32* %_p, i32 %n) #0 {
entry:
  br label %do.body

do.body:                                          ; preds = %do.body, %entry
  %n.addr.0 = phi i32 [ %n, %entry ], [ %dec, %do.body ]
  %0 = call i8* @llvm.noalias.decl.p0i8.p0p0i32.i32(i32** null, i32 0, metadata !11)
  %1 = call i32* @llvm.side.noalias.p0i32.p0i8.p0p0i32.p0p0i32.i32(i32* %_p, i8* %0, i32** null, i32** undef, i32 0, metadata !11), !tbaa !5, !noalias !11
  store i32 42, i32* %_p, noalias_sidechannel i32* %1, align 4, !tbaa !9, !noalias !11
  %dec = add nsw i32 %n.addr.0, -1
  %cmp = icmp ne i32 %dec, 0
  br i1 %cmp, label %do.body, label %do.end

do.end:                                           ; preds = %do.body
  ret void
}

; CHECK-LABEL: @test02(
; CHECK-LABEL: entry:
; CHECK-LABEL: do.body:
; CHECK:  %0 = call i8* @llvm.noalias.decl.p0i8.p0p0i32.i32(i32** null, i32 0, metadata !11)
; CHECK:  %1 = call i32* @llvm.side.noalias.p0i32.p0i8.p0p0i32.p0p0i32.i32(i32* %_p, i8* %0, i32** null, i32** undef, i32 0, metadata !11), !tbaa !5, !noalias !11
; CHECK-LABEL: do.end:
; CHECK: store i32 42, i32* %_p, align 4, !tbaa !9
; CHECK: ret void

; Function Attrs: argmemonly nounwind
declare i8* @llvm.noalias.decl.p0i8.p0p0i32.i32(i32**, i32, metadata) #1

; Function Attrs: nounwind readnone speculatable
declare i32* @llvm.side.noalias.p0i32.p0i8.p0p0i32.p0p0i32.i32(i32*, i8*, i32**, i32**, i32, metadata) #2

attributes #0 = { nounwind "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { argmemonly nounwind }
attributes #2 = { nounwind readnone speculatable }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang"}
!2 = !{!3}
!3 = distinct !{!3, !4, !"test01: rp"}
!4 = distinct !{!4, !"test01"}
!5 = !{!6, !6, i64 0, i64 4}
!6 = !{!7, i64 4, !"any pointer"}
!7 = !{!8, i64 1, !"omnipotent char"}
!8 = !{!"Simple C/C++ TBAA"}
!9 = !{!10, !10, i64 0, i64 4}
!10 = !{!7, i64 4, !"int"}
!11 = !{!12}
!12 = distinct !{!12, !13, !"test02: rp"}
!13 = distinct !{!13, !"test02"}
