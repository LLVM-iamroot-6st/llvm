; ModuleID = 'test.br.bc'

@aberrormsg = internal constant [35 x i8] c"Error: The head has left the tape.\00"

; Function Attrs: nounwind
declare void @llvm.memset.p0i8.i32(i8* nocapture, i8, i32, i32, i1) #0

declare i32 @getchar()

declare i32 @putchar(i32)

define void @brainf() {
brainf:
  %malloccall = tail call i8* @malloc(i32 mul (i32 ptrtoint (i8* getelementptr (i8* null, i32 1) to i32), i32 65536))
  call void @llvm.memset.p0i8.i32(i8* %malloccall, i8 0, i32 65536, i32 1, i1 false)
  %arrmax = getelementptr i8* %malloccall, i32 65536
  %head = getelementptr i8* %malloccall, i32 32768
  %tape = call i32 @getchar()
  %tape3 = trunc i32 %tape to i8
  store i8 %tape3, i8* %head
  %tape4 = load i8* %head
  %tape5 = sext i8 %tape4 to i32
  %0 = call i32 @putchar(i32 %tape5)
  %tape6 = load i8* %head
  %tape7 = add i8 %tape6, 3
  store i8 %tape7, i8* %head
  %head8 = getelementptr i8* %head, i32 1
  %test = icmp uge i8* %head8, %arrmax
  %test9 = icmp ult i8* %head8, %malloccall
  %test10 = or i1 %test, %test9
  br i1 %test10, label %brainf2, label %brainf11

brainf1:                                          ; preds = %brainf27, %brainf2
  tail call void @free(i8* %malloccall)
  ret void

brainf2:                                          ; preds = %brainf21, %brainf
  %1 = call i32 @puts(i8* getelementptr inbounds ([35 x i8]* @aberrormsg, i32 0, i32 0))
  br label %brainf1

brainf11:                                         ; preds = %brainf
  br label %brainf12

brainf12:                                         ; preds = %brainf13, %brainf11
  %head14 = phi i8* [ %head8, %brainf11 ], [ %head14, %brainf13 ]
  %tape19 = load i8* %head14
  %test20 = icmp eq i8 %tape19, 0
  br i1 %test20, label %brainf21, label %brainf13

brainf13:                                         ; preds = %brainf12
  %tape15 = load i8* %head14
  %tape16 = sext i8 %tape15 to i32
  %2 = call i32 @putchar(i32 %tape16)
  %tape17 = load i8* %head14
  %tape18 = add i8 %tape17, -1
  store i8 %tape18, i8* %head14
  br label %brainf12

brainf21:                                         ; preds = %brainf12
  %head22 = phi i8* [ %head14, %brainf12 ]
  %head23 = getelementptr i8* %head22, i32 1
  %test24 = icmp uge i8* %head23, %arrmax
  %test25 = icmp ult i8* %head23, %malloccall
  %test26 = or i1 %test24, %test25
  br i1 %test26, label %brainf2, label %brainf27

brainf27:                                         ; preds = %brainf21
  %tape28 = call i32 @getchar()
  %tape29 = trunc i32 %tape28 to i8
  store i8 %tape29, i8* %head23
  %tape30 = load i8* %head23
  %tape31 = sext i8 %tape30 to i32
  %3 = call i32 @putchar(i32 %tape31)
  br label %brainf1
}

declare noalias i8* @malloc(i32)

declare void @free(i8*)

declare i32 @puts(i8*)

define i32 @main(i32 %argc, i8** %argv) {
main.0:
  call void @brainf()
  ret i32 0
}

attributes #0 = { nounwind }
