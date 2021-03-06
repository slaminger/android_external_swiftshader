; Test whether we don't mangle external, undefined, global names. This
; feature is needed for cross tests on global initializer relocations.
;
; Note: This code was generated by compiling subzero/crosstest/test_global.cpp

; We use lc2i (rather than p2i) because PNaCl bitcode files do not
; allow externally defined global variables. Hence, this test can only
; work if we read LLVM IR source, and convert to to ICE.

; REQUIRES: allow_llvm_ir_as_input
; RUN: %lc2i -i %s --insts --args --allow-uninitialized-globals \
; RUN:       -allow-externally-defined-symbols | FileCheck %s
; RUN: %lc2i -i %s --insts --args --allow-uninitialized-globals \
; RUN:       -allow-externally-defined-symbols \
; RUN:       -prefix Subzero_ | FileCheck --check-prefix=CROSS %s

@ArrayInitPartial = internal global [40 x i8] c"<\00\00\00F\00\00\00P\00\00\00Z\00\00\00d\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00", align 4

; CHECK: @ArrayInitPartial = internal global [40 x i8] c"<\00\00\00F\00\00\00P\00\00\00Z\00\00\00d\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00", align 4
; CROSS: @Subzero_ArrayInitPartial = internal global [40 x i8] c"<\00\00\00F\00\00\00P\00\00\00Z\00\00\00d\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00", align 4

@ArrayInitFull = internal global [20 x i8] c"\0A\00\00\00\14\00\00\00\1E\00\00\00(\00\00\002\00\00\00", align 4

; CHECK: @ArrayInitFull = internal global [20 x i8] c"\0A\00\00\00\14\00\00\00\1E\00\00\00(\00\00\002\00\00\00", align 4
; CROSS: @Subzero_ArrayInitFull = internal global [20 x i8] c"\0A\00\00\00\14\00\00\00\1E\00\00\00(\00\00\002\00\00\00", align 4

@NumArraysElements = internal global [4 x i8] c"\06\00\00\00", align 4

; CHECK: @NumArraysElements = internal global [4 x i8] c"\06\00\00\00", align 4
; CROSS: @Subzero_NumArraysElements = internal global [4 x i8] c"\06\00\00\00", align 4

@Arrays = internal constant <{ i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8] }> <{ i32 ptrtoint ([40 x i8]* @ArrayInitPartial to i32), [4 x i8] c"(\00\00\00", i32 ptrtoint ([20 x i8]* @ArrayInitFull to i32), [4 x i8] c"\14\00\00\00", i32 ptrtoint ([12 x i8]* @_ZL10ArrayConst to i32), [4 x i8] c"\0C\00\00\00", i32 ptrtoint ([80 x i8]* @_ZL11ArrayDouble to i32), [4 x i8] c"P\00\00\00", i32 add (i32 ptrtoint ([40 x i8]* @ArrayInitPartial to i32), i32 8), [4 x i8] c" \00\00\00", i32 ptrtoint ([80 x i8]* @_ZL8StructEx to i32), [4 x i8] c"P\00\00\00" }>, align 4

; CHECK: @Arrays = internal constant <{ i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8] }> <{ i32 ptrtoint ([40 x i8]* @ArrayInitPartial to i32), [4 x i8] c"(\00\00\00", i32 ptrtoint ([20 x i8]* @ArrayInitFull to i32), [4 x i8] c"\14\00\00\00", i32 ptrtoint ([12 x i8]* @_ZL10ArrayConst to i32), [4 x i8] c"\0C\00\00\00", i32 ptrtoint ([80 x i8]* @_ZL11ArrayDouble to i32), [4 x i8] c"P\00\00\00", i32 add (i32 ptrtoint ([40 x i8]* @ArrayInitPartial to i32), i32 8), [4 x i8] c" \00\00\00", i32 ptrtoint ([80 x i8]* @_ZL8StructEx to i32), [4 x i8] c"P\00\00\00" }>, align 4
; CROSS: @Subzero_Arrays = internal constant <{ i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8] }> <{ i32 ptrtoint ([40 x i8]* @Subzero_ArrayInitPartial to i32), [4 x i8] c"(\00\00\00", i32 ptrtoint ([20 x i8]* @Subzero_ArrayInitFull to i32), [4 x i8] c"\14\00\00\00", i32 ptrtoint ([12 x i8]* @Subzero__ZL10ArrayConst to i32), [4 x i8] c"\0C\00\00\00", i32 ptrtoint ([80 x i8]* @Subzero__ZL11ArrayDouble to i32), [4 x i8] c"P\00\00\00", i32 add (i32 ptrtoint ([40 x i8]* @Subzero_ArrayInitPartial to i32), i32 8), [4 x i8] c" \00\00\00", i32 ptrtoint ([80 x i8]* @Subzero__ZL8StructEx to i32), [4 x i8] c"P\00\00\00" }>, align 4


@_ZL10ArrayConst = internal constant [12 x i8] c"\F6\FF\FF\FF\EC\FF\FF\FF\E2\FF\FF\FF", align 4

; CHECK: @_ZL10ArrayConst = internal constant [12 x i8] c"\F6\FF\FF\FF\EC\FF\FF\FF\E2\FF\FF\FF", align 4
; CROSS: @Subzero__ZL10ArrayConst = internal constant [12 x i8] c"\F6\FF\FF\FF\EC\FF\FF\FF\E2\FF\FF\FF", align 4

@_ZL11ArrayDouble = internal global [80 x i8] c"\00\00\00\00\00\00\E0?\00\00\00\00\00\00\F8?\00\00\00\00\00\00\04@\00\00\00\00\00\00\0C@\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00", align 8

; CHECK: @_ZL11ArrayDouble = internal global [80 x i8] c"\00\00\00\00\00\00\E0?\00\00\00\00\00\00\F8?\00\00\00\00\00\00\04@\00\00\00\00\00\00\0C@\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00", align 8
; CROSS: @Subzero__ZL11ArrayDouble = internal global [80 x i8] c"\00\00\00\00\00\00\E0?\00\00\00\00\00\00\F8?\00\00\00\00\00\00\04@\00\00\00\00\00\00\0C@\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00", align 8

@_ZL8StructEx = internal global [80 x i8] zeroinitializer, align 8

; CHECK: @_ZL8StructEx = internal global [80 x i8] zeroinitializer, align 8
; CROSS: @Subzero__ZL8StructEx = internal global [80 x i8] zeroinitializer, align 8

@ExternName1 = external global [4 x i8], align 4

; CHECK: @ExternName1 = external global <{  }> <{  }>, align 4
; CROSS: @ExternName1 = external global <{  }> <{  }>, align 4

@ExternName4 = external global [4 x i8], align 4

; CHECK: @ExternName4 = external global <{  }> <{  }>, align 4
; CROSS: @ExternName4 = external global <{  }> <{  }>, align 4

@ExternName3 = external global [4 x i8], align 4

; CHECK: @ExternName3 = external global <{  }> <{  }>, align 4
; CROSS: @ExternName3 = external global <{  }> <{  }>, align 4

@ExternName2 = external global [4 x i8], align 4

; CHECK: @ExternName2 = external global <{  }> <{  }>, align 4
; CROSS: @ExternName2 = external global <{  }> <{  }>, align 4

@ExternName5 = external global [4 x i8], align 4

; CHECK: @ExternName5 = external global <{  }> <{  }>, align 4
; CROSS: @ExternName5 = external global <{  }> <{  }>, align 4

define i32 @_Z12getNumArraysv() {
; CHECK: define i32 @_Z12getNumArraysv() {
; CROSS: define i32 @_ZN8Subzero_12getNumArraysEv() {
entry:
  %NumArraysElements.bc = bitcast [4 x i8]* @NumArraysElements to i32*
; CHECK:   %NumArraysElements.bc = bitcast i32 @NumArraysElements to i32
; CROSS:   %NumArraysElements.bc = bitcast i32 @Subzero_NumArraysElements to i32
  %0 = load i32, i32* %NumArraysElements.bc, align 1
  ret i32 %0
}

define i32 @_Z8getArrayjRj(i32 %WhichArray, i32 %Len) {
; CHECK: define i32 @_Z8getArrayjRj(i32 %WhichArray, i32 %Len) {
; CROSS: define i32 @_ZN8Subzero_8getArrayEjRj(i32 %WhichArray, i32 %Len) {
entry:
  %NumArraysElements.bc = bitcast [4 x i8]* @NumArraysElements to i32*
; CHECK:   %NumArraysElements.bc = bitcast i32 @NumArraysElements to i32
; CROSS:   %NumArraysElements.bc = bitcast i32 @Subzero_NumArraysElements to i32
  %0 = load i32, i32* %NumArraysElements.bc, align 1
  %cmp = icmp ugt i32 %0, %WhichArray
; CHECK:   %cmp = icmp ugt i32 %__3, %WhichArray
; CROSS:   %cmp = icmp ugt i32 %__3, %WhichArray
  br i1 %cmp, label %if.end, label %if.then

if.then:                                          ; preds = %entry
  %Len.asptr = inttoptr i32 %Len to i32*
; CHECK:   %Len.asptr = i32 %Len
; CROSS:   %Len.asptr = i32 %Len
  store i32 -1, i32* %Len.asptr, align 1
  br label %return

if.end:                                           ; preds = %entry
  %gep_array = mul i32 %WhichArray, 8
; CHECK:   %gep_array = mul i32 %WhichArray, 8
; CROSS:   %gep_array = mul i32 %WhichArray, 8
  %expanded1 = ptrtoint <{ i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8] }>* @Arrays to i32
; CHECK:   %expanded1 = i32 @Arrays
; CROSS:   %expanded1 = i32 @Subzero_Arrays
  %gep = add i32 %expanded1, %gep_array
  %gep1 = add i32 %gep, 4
  %gep1.asptr = inttoptr i32 %gep1 to i32*
  %1 = load i32, i32* %gep1.asptr, align 1
  %Len.asptr3 = inttoptr i32 %Len to i32*
; CHECK:   %Len.asptr3 = i32 %Len
; CROSS:   %Len.asptr3 = i32 %Len
  store i32 %1, i32* %Len.asptr3, align 1
  %gep_array3 = mul i32 %WhichArray, 8
; CHECK:   %gep_array3 = mul i32 %WhichArray, 8
; CROSS:   %gep_array3 = mul i32 %WhichArray, 8
  %expanded2 = ptrtoint <{ i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8], i32, [4 x i8] }>* @Arrays to i32
; CHECK:   %expanded2 = i32 @Arrays
; CROSS:   %expanded2 = i32 @Subzero_Arrays
  %gep4 = add i32 %expanded2, %gep_array3
  %gep4.asptr = inttoptr i32 %gep4 to i32*
  %2 = load i32, i32* %gep4.asptr, align 1
  br label %return

return:                                           ; preds = %if.end, %if.then
  %retval.0 = phi i32 [ 0, %if.then ], [ %2, %if.end ]
  ret i32 %retval.0
}

define void @_GLOBAL__I_a() {
; CHECK: define void @_GLOBAL__I_a() {
; CROSS: define void @Subzero__GLOBAL__I_a() {
entry:
  %_ZL8StructEx.bc = bitcast [80 x i8]* @_ZL8StructEx to i32*
; CHECK:   %_ZL8StructEx.bc = bitcast i32 @_ZL8StructEx to i32
; CROSS:   %_ZL8StructEx.bc = bitcast i32 @Subzero__ZL8StructEx to i32
  store i32 10, i32* %_ZL8StructEx.bc, align 1
  %expanded1 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded1 = i32 @_ZL8StructEx
; CROSS:   %expanded1 = i32 @Subzero__ZL8StructEx
  %gep = add i32 %expanded1, 4
  %gep.asptr = inttoptr i32 %gep to i32*
  store i32 20, i32* %gep.asptr, align 1
  %expanded2 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded2 = i32 @_ZL8StructEx
; CROSS:   %expanded2 = i32 @Subzero__ZL8StructEx
  %gep18 = add i32 %expanded2, 8
  %gep18.asptr = inttoptr i32 %gep18 to i32*
  store i32 30, i32* %gep18.asptr, align 1
  %expanded3 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded3 = i32 @_ZL8StructEx
; CROSS:   %expanded3 = i32 @Subzero__ZL8StructEx
  %gep20 = add i32 %expanded3, 12
  %gep20.asptr = inttoptr i32 %gep20 to i32*
  store i32 40, i32* %gep20.asptr, align 1
  %expanded4 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded4 = i32 @_ZL8StructEx
; CROSS:   %expanded4 = i32 @Subzero__ZL8StructEx
  %gep22 = add i32 %expanded4, 16
  %gep22.asptr = inttoptr i32 %gep22 to i32*
  store i32 50, i32* %gep22.asptr, align 1
  %ExternName1.bc = bitcast [4 x i8]* @ExternName1 to i32*
; CHECK:   %ExternName1.bc = bitcast i32 @ExternName1 to i32
; CROSS:   %ExternName1.bc = bitcast i32 @ExternName1 to i32
  %0 = load i32, i32* %ExternName1.bc, align 1
  %expanded6 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded6 = i32 @_ZL8StructEx
; CROSS:   %expanded6 = i32 @Subzero__ZL8StructEx
  %gep24 = add i32 %expanded6, 20
  %gep24.asptr = inttoptr i32 %gep24 to i32*
  store i32 %0, i32* %gep24.asptr, align 1
  %expanded7 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded7 = i32 @_ZL8StructEx
; CROSS:   %expanded7 = i32 @Subzero__ZL8StructEx
  %gep26 = add i32 %expanded7, 24
  %gep26.asptr = inttoptr i32 %gep26 to double*
  store double 5.000000e-01, double* %gep26.asptr, align 8
  %expanded8 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded8 = i32 @_ZL8StructEx
; CROSS:   %expanded8 = i32 @Subzero__ZL8StructEx
  %gep28 = add i32 %expanded8, 32
  %gep28.asptr = inttoptr i32 %gep28 to double*
  store double 1.500000e+00, double* %gep28.asptr, align 8
  %expanded9 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded9 = i32 @_ZL8StructEx
; CROSS:   %expanded9 = i32 @Subzero__ZL8StructEx
  %gep30 = add i32 %expanded9, 40
  %gep30.asptr = inttoptr i32 %gep30 to double*
  store double 2.500000e+00, double* %gep30.asptr, align 8
  %ExternName4.bc = bitcast [4 x i8]* @ExternName4 to i32*
; CHECK:   %ExternName4.bc = bitcast i32 @ExternName4 to i32
; CROSS:   %ExternName4.bc = bitcast i32 @ExternName4 to i32
  %1 = load i32, i32* %ExternName4.bc, align 1
  %expanded11 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded11 = i32 @_ZL8StructEx
; CROSS:   %expanded11 = i32 @Subzero__ZL8StructEx
  %gep32 = add i32 %expanded11, 48
  %gep32.asptr = inttoptr i32 %gep32 to i32*
  store i32 %1, i32* %gep32.asptr, align 1
  %ExternName3.bc = bitcast [4 x i8]* @ExternName3 to i32*
; CHECK:   %ExternName3.bc = bitcast i32 @ExternName3 to i32
; CROSS:   %ExternName3.bc = bitcast i32 @ExternName3 to i32
  %2 = load i32, i32* %ExternName3.bc, align 1
  %expanded13 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded13 = i32 @_ZL8StructEx
; CROSS:   %expanded13 = i32 @Subzero__ZL8StructEx
  %gep34 = add i32 %expanded13, 52
  %gep34.asptr = inttoptr i32 %gep34 to i32*
  store i32 %2, i32* %gep34.asptr, align 1
  %expanded14 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded14 = i32 @_ZL8StructEx
; CROSS:   %expanded14 = i32 @Subzero__ZL8StructEx
  %gep36 = add i32 %expanded14, 56
  %gep36.asptr = inttoptr i32 %gep36 to i32*
  store i32 1000, i32* %gep36.asptr, align 1
  %expanded15 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded15 = i32 @_ZL8StructEx
; CROSS:   %expanded15 = i32 @Subzero__ZL8StructEx
  %gep38 = add i32 %expanded15, 60
  %gep38.asptr = inttoptr i32 %gep38 to i32*
  store i32 1010, i32* %gep38.asptr, align 1
  %expanded16 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded16 = i32 @_ZL8StructEx
; CROSS:   %expanded16 = i32 @Subzero__ZL8StructEx
  %gep40 = add i32 %expanded16, 64
  %gep40.asptr = inttoptr i32 %gep40 to i32*
  store i32 1020, i32* %gep40.asptr, align 1
  %ExternName2.bc = bitcast [4 x i8]* @ExternName2 to i32*
; CHECK:   %ExternName2.bc = bitcast i32 @ExternName2 to i32
; CROSS:   %ExternName2.bc = bitcast i32 @ExternName2 to i32
  %3 = load i32, i32* %ExternName2.bc, align 1
  %expanded18 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded18 = i32 @_ZL8StructEx
; CROSS:   %expanded18 = i32 @Subzero__ZL8StructEx
  %gep42 = add i32 %expanded18, 68
  %gep42.asptr = inttoptr i32 %gep42 to i32*
  store i32 %3, i32* %gep42.asptr, align 1
  %ExternName5.bc = bitcast [4 x i8]* @ExternName5 to i32*
; CHECK:   %ExternName5.bc = bitcast i32 @ExternName5 to i32
; CROSS:   %ExternName5.bc = bitcast i32 @ExternName5 to i32
  %4 = load i32, i32* %ExternName5.bc, align 1
  %expanded20 = ptrtoint [80 x i8]* @_ZL8StructEx to i32
; CHECK:   %expanded20 = i32 @_ZL8StructEx
; CROSS:   %expanded20 = i32 @Subzero__ZL8StructEx
  %gep44 = add i32 %expanded20, 72
  %gep44.asptr = inttoptr i32 %gep44 to i32*
  store i32 %4, i32* %gep44.asptr, align 1
  ret void
}

define i32 @nacl_tp_tdb_offset(i32) {
entry:
  ret i32 0
}

define i32 @nacl_tp_tls_offset(i32 %size) {
entry:
  %result = sub i32 0, %size
  ret i32 %result
}
