; First example from Doc/Coroutines.rst (two block loop) converted to retcon
; RUN: opt < %s -enable-coroutines -O2 -S | FileCheck %s

define {i8*, i32} @f(i8* %buffer, i32 %n) {
entry:
  %id = call token @llvm.coro.id.retcon(i32 8, i32 4, i8* %buffer, i8* bitcast ({i8*, i32} (i8*, i8)* @prototype to i8*), i8* bitcast (i8* (i32)* @allocate to i8*), i8* bitcast (void (i8*)* @deallocate to i8*))
  %hdl = call i8* @llvm.coro.begin(token %id, i8* null)
  br label %loop

loop:
  %n.val = phi i32 [ %n, %entry ], [ %inc, %resume ]
  %unwind = call i8 (...) @llvm.coro.suspend.retcon.i8(i32 %n.val)
  %unwind0 = icmp ne i8 %unwind, 0
  br i1 %unwind0, label %cleanup, label %resume

resume:
  %inc = add i32 %n.val, 1
  br label %loop

cleanup:
  call i1 @llvm.coro.end(i8* %hdl, i1 0)
  unreachable
}

; CHECK-LABEL: define { i8*, i32 } @f(i8* %buffer, i32 %n)
; CHECK-NEXT:  entry:
; CHECK-NEXT:    [[T0:%.*]] = bitcast i8* %buffer to i32*
; CHECK-NEXT:    store i32 %n, i32* [[T0]], align 4
; CHECK-NEXT:    [[RET:%.*]] = insertvalue { i8*, i32 } { i8* bitcast ({ i8*, i32 } (i8*, i8)* @f.resume.0 to i8*), i32 undef }, i32 %n, 1
; CHECK-NEXT:    ret { i8*, i32 } [[RET]]
; CHECK-NEXT:  }

; CHECK-LABEL: define internal { i8*, i32 } @f.resume.0(i8* noalias nonnull %0, i8 zeroext %1)
; CHECK-NEXT:  :
; CHECK-NEXT:    [[T0:%.*]] = icmp eq i8 %1, 0
; CHECK-NEXT:    br i1 [[T0]],
; CHECK:       :
; CHECK-NEXT:    [[T0:%.*]] = bitcast i8* %0 to i32*
; CHECK-NEXT:    [[T1:%.*]] = load i32, i32* [[T0]], align 4
; CHECK-NEXT:    %inc = add i32 [[T1]], 1
; CHECK-NEXT:    store i32 %inc, i32* [[T0]], align 4
; CHECK-NEXT:    [[RET:%.*]] = insertvalue { i8*, i32 } { i8* bitcast ({ i8*, i32 } (i8*, i8)* @f.resume.0 to i8*), i32 undef }, i32 %inc, 1
; CHECK-NEXT:    ret { i8*, i32 } [[RET]]
; CHECK:       :
; CHECK-NEXT:    ret { i8*, i32 } { i8* null, i32 undef }
; CHECK-NEXT:  }

define i32 @main() {
entry:
  %0 = alloca [8 x i8], align 4
  %buffer = bitcast [8 x i8]* %0 to i8*
  %prepare = call i8* @llvm.coro.prepare.retcon(i8* bitcast ({i8*, i32} (i8*, i32)* @f to i8*))
  %f = bitcast i8* %prepare to {i8*, i32} (i8*, i32)*
  %result0 = call {i8*, i32} %f(i8* %buffer, i32 4)
  %value0 = extractvalue {i8*, i32} %result0, 1
  call void @print(i32 %value0)
  %cont0 = extractvalue {i8*, i32} %result0, 0
  %cont0.cast = bitcast i8* %cont0 to {i8*, i32} (i8*, i8)*
  %result1 = call {i8*, i32} %cont0.cast(i8* %buffer, i8 zeroext 0)
  %value1 = extractvalue {i8*, i32} %result1, 1
  call void @print(i32 %value1)
  %cont1 = extractvalue {i8*, i32} %result1, 0
  %cont1.cast = bitcast i8* %cont1 to {i8*, i32} (i8*, i8)*
  %result2 = call {i8*, i32} %cont1.cast(i8* %buffer, i8 zeroext 0)
  %value2 = extractvalue {i8*, i32} %result2, 1
  call void @print(i32 %value2)
  %cont2 = extractvalue {i8*, i32} %result2, 0
  %cont2.cast = bitcast i8* %cont2 to {i8*, i32} (i8*, i8)*
  call {i8*, i32} %cont2.cast(i8* %buffer, i8 zeroext 1)
  ret i32 0
}

;   Unfortunately, we don't seem to fully optimize this right now due
;   to some sort of phase-ordering thing.
; CHECK-LABEL: define i32 @main
; CHECK-NEXT:  entry:
; CHECK:         [[BUFFER:%.*]] = alloca [8 x i8], align 4
; CHECK:         [[SUB:%.*]] = getelementptr inbounds [8 x i8], [8 x i8]* [[BUFFER]], i64 0, i64 0
; CHECK:         [[SLOT:%.*]] = bitcast [8 x i8]* [[BUFFER]] to i32*
; CHECK-NEXT:    store i32 4, i32* [[SLOT]], noalias_sidechannel i32* undef, align 4
; CHECK-NEXT:    call void @print(i32 4)
; CHECK-NEXT:    [[SUB_0_DECL:%.*]] = call i8* @llvm.noalias.decl.p0i8.p0p0i8.i64(i8** null, i64 0, metadata [[SUB_0_META:!.*]])
; CHECK-NEXT:    [[SUB_0_SIDE:%.*]] = call i8* @llvm.side.noalias.p0i8.p0i8.p0p0i8.p0p0i8.i64(i8* nonnull [[SUB]], i8* [[SUB_0_DECL]], i8** null, i8** undef, i64 0, metadata [[SUB_0_META]])
; CHECK-NEXT:    [[SUB_0_SIDE_I32:%.*]] = bitcast i8* [[SUB_0_SIDE]] to i32*
; CHECK-NEXT:    [[LOAD:%.*]] = load i32, i32* [[SLOT]], noalias_sidechannel i32* [[SUB_0_SIDE_I32]], align 4, !noalias [[SUB_0_META]]
; CHECK-NEXT:    [[INC:%.*]] = add i32 [[LOAD]], 1
; CHECK-NEXT:    store i32 [[INC]], i32* [[SLOT]], noalias_sidechannel i32* [[SUB_0_SIDE_I32]], align 4, !noalias [[SUB_0_META]]
; CHECK-NEXT:    call void @print(i32 [[INC]])
; CHECK-NEXT:    [[SUB_1_DECL:%.*]] = call i8* @llvm.noalias.decl.p0i8.p0p0i8.i64(i8** null, i64 0, metadata [[SUB_1_META:!.*]])
; CHECK-NEXT:    [[SUB_1_SIDE:%.*]] = call i8* @llvm.side.noalias.p0i8.p0i8.p0p0i8.p0p0i8.i64(i8* nonnull [[SUB]], i8* [[SUB_1_DECL]], i8** null, i8** undef, i64 0, metadata [[SUB_1_META]])
; CHECK-NEXT:    [[SUB_1_SIDE_I32:%.*]] = bitcast i8* [[SUB_1_SIDE]] to i32*
; CHECK-NEXT:    [[LOAD:%.*]] = load i32, i32* [[SLOT]], noalias_sidechannel i32* [[SUB_1_SIDE_I32]], align 4, !noalias [[SUB_1_META]]
; CHECK-NEXT:    [[INC:%.*]] = add i32 [[LOAD]], 1
; CHECK-NEXT:    store i32 [[INC]], i32* [[SLOT]], noalias_sidechannel i32* [[SUB_1_SIDE_I32]], align 4, !noalias [[SUB_1_META]]
; CHECK-NEXT:    call void @print(i32 [[INC]])
; CHECK-NEXT:    ret i32 0

declare token @llvm.coro.id.retcon(i32, i32, i8*, i8*, i8*, i8*)
declare i8* @llvm.coro.begin(token, i8*)
declare i8 @llvm.coro.suspend.retcon.i8(...)
declare i1 @llvm.coro.end(i8*, i1)
declare i8* @llvm.coro.prepare.retcon(i8*)

declare {i8*, i32} @prototype(i8*, i8 zeroext)

declare noalias i8* @allocate(i32 %size)
declare void @deallocate(i8* %ptr)

declare void @print(i32)

