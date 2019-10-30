; RUN: opt --verify -S < %s | FileCheck %s
; JDO_FIXME_NOW: fix the bicode support.. R U N: llvm-as < %s | llvm-dis | FileCheck %s
; JDO_FIXME_NOW: R U N: verify-uselistorder < %s

define i32 @f(i32* %p, i32* %q) {
  store i32 42, i32* %p, noalias_sidechannel i32* %p
  store i32 43, i32* %q, noalias_sidechannel i32* %q
  %r = load i32, i32* %p, noalias_sidechannel i32* %p
  ret i32 %r
}

; CHECK:      define i32 @f(i32* %p, i32* %q) {
; CHECK-NEXT:   store i32 42, i32* %p, noalias_sidechannel i32* %p
; CHECK-NEXT:   store i32 43, i32* %q, noalias_sidechannel i32* %q
; CHECK-NEXT:   %r = load i32, i32* %p, noalias_sidechannel i32* %p
; CHECK-NEXT:   ret i32 %r
; CHECK-NEXT: }
