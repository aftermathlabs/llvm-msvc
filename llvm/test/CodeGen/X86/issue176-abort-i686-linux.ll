; RUN: llc < %s -mtriple=i686-pc-linux-gnu -relocation-model=pic | FileCheck %s
; RUN: llc < %s -mtriple=i686-unknown-linux-gnu -relocation-model=pic | FileCheck %s
; RUN: llc < %s -mtriple=i686-linux-android -relocation-model=pic | FileCheck %s

declare void @abort() nounwind

define void @f() local_unnamed_addr {
entry:
  tail call void @abort() nounwind
  unreachable
}

; CHECK-LABEL: f:
; CHECK: calll
