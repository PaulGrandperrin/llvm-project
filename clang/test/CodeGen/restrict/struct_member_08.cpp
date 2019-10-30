// RUN: %clang_cc1 -triple x86_64-apple-darwin -O2 -ffull-restrict %s -emit-llvm -o - | FileCheck %s

// NOTE: this test in C++ mode

struct Fum {
  Fum(unsigned long long d) {
    ptr1=((int*)(d & 0xffffffff));
    ptr2=((int*)((d >> 32)& 0xffffffff));
  }
  Fum(const Fum&) = default;

  int* __restrict ptr1;
  int* __restrict ptr2;
};

static Fum pass(Fum d) { return d; }

int test_Fum_01(unsigned long long data, int* p1) {
  Fum tmp={data};

  int* p0=tmp.ptr1;

  *p0=42;
  *p1=99;
  return *p0;
}
// CHECK: @_Z11test_Fum_01yPi
// CHECK: ret i32 42

int test_Fum_02(unsigned long long data) {
  Fum tmp={data};

  int* p0=tmp.ptr1;
  int* p1=tmp.ptr2;

  *p0=42;
  *p1=99;
  return *p0;
}
// CHECK: @_Z11test_Fum_02y
// CHECK: ret i32 42

int test_Fum_pass_01(unsigned long long data, int* p1) {
  Fum tmp={data};

  int* p0=pass(tmp).ptr1;

  *p0=42;
  *p1=99;
  return *p0;
}
// CHECK: @_Z16test_Fum_pass_01yPi
// CHECK: ret i32 42


int test_Fum_pass_02(unsigned long long data) {
  Fum tmp={data};

  int* p0=pass(tmp).ptr1;
  int* p1=pass(tmp).ptr2;

  *p0=42;
  *p1=99;
  return *p0;
}
// CHECK: @_Z16test_Fum_pass_02y
// CHECK: ret i32 42
