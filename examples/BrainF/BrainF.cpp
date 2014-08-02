//===-- BrainF.cpp - BrainF compiler example ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//
//
// This class compiles the BrainF language into LLVM assembly.
//
// The BrainF language has 8 commands:
// Command   Equivalent C    Action
// -------   ------------    ------
// ,         *h=getchar();   Read a character from stdin, 255 on EOF
// .         putchar(*h);    Write a character to stdout
// -         --*h;           Decrement tape
// +         ++*h;           Increment tape
// <         --h;            Move head left
// >         ++h;            Move head right
// [         while(*h) {     Start loop
// ]         }               End loop
//
//===--------------------------------------------------------------------===//

#include "BrainF.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include <iostream>
using namespace llvm;

//Set the constants for naming
const char *BrainF::tapereg = "tape";
const char *BrainF::headreg = "head";
const char *BrainF::label   = "brainf";
const char *BrainF::testreg = "test";

Module *BrainF::parse(std::istream *in1, int mem, CompileFlags cf,
                      LLVMContext& Context) {
  in       = in1;
  memtotal = mem;
  comflag  = cf;

  header(Context);
  readloop(0, 0, 0, Context);
  delete builder;
  return module;
}

void BrainF::header(LLVMContext& C) {
  module = new Module("BrainF", C);

  //Function prototypes

  //declare void @llvm.memset.p0i8.i32(i8 *, i8, i32, i32, i1)
  // Type : int8*, int32
  Type *Tys[] = { Type::getInt8PtrTy(C), Type::getInt32Ty(C) };
  Function *memset_func = Intrinsic::getDeclaration(module, Intrinsic::memset,
                                                    Tys);

  //declare i32 @getchar()
  getchar_func = cast<Function>(module->
    getOrInsertFunction("getchar", IntegerType::getInt32Ty(C), NULL));

  //declare i32 @putchar(i32)
  putchar_func = cast<Function>(module->
    getOrInsertFunction("putchar", IntegerType::getInt32Ty(C),
                        IntegerType::getInt32Ty(C), NULL));


  //Function header

  //define void @brainf()
  brainf_func = cast<Function>(module->
    getOrInsertFunction("brainf", Type::getVoidTy(C), NULL));

  //brainf 함수의 BasicBlock
  /** 20140728 [eundoo.song]
   *  1) BaiscBlock::Create
   *  - 생성된 함수 brainf의 entry Basic Block 을 생성
   *  - basicblock list의 끝에 추가한후 BasicBlock 자체를 반환
   *    : header -> BasicBlock(brainf)
   *
   *  2) new IRBuilder<>(...)
   */ 
  builder = new IRBuilder<>(BasicBlock::Create(C, label, brainf_func));

  // Memory allocation
  //%arr = malloc i8, i32 %d
  // int 값을 사용하기 위해 Mapping Class
  //memtotal : 64kb
  //APInt(unsigned numBits, uint64_t val, bool isSigned = false)
  //BitWidth가 32비트인 64Kb 의 val_mem::ConstantInt를 얻음.
  ConstantInt *val_mem = ConstantInt::get(C, APInt(32, memtotal));
  BasicBlock* BB = builder->GetInsertBlock();
  /** 20140731 [eundoo.song]
   * 미리 정해져 있는 LLVM 기본 타입 리턴
   * LLVM Basic(built-in) type instances.
	   Type VoidTy, LabelTy, HalfTy, FloatTy, DoubleTy, MetadataTy;
       Type X86_FP80Ty, FP128Ty, PPC_FP128Ty, X86_MMXTy;
       IntegerType Int1Ty, Int8Ty, Int16Ty, Int32Ty, Int64Ty;
   * IntegerType Decl.
   *   explicit IntegerType(LLVMContext &C, unsigned NumBits) : Type(C, IntegerTyID){
   * Init.
   *   Int32Ty(C, 32),
   * return &C.pImpl->Int32Ty;
   */
  Type* IntPtrTy = IntegerType::getInt32Ty(C);//32 bit type
  /** 20140731 [eundoo.song]
   * 미리 정해져 있는 LLVM 기본 타입 리턴
   * Init. 
   *	Int8Ty(C, 8),
   * return &C.pImpl->Int8Ty;
   */
  Type* Int8Ty = IntegerType::getInt8Ty(C);//8 bit type
  //1byte(8bit)를 반환할것으로 예상
  Constant* allocsize = ConstantExpr::getSizeOf(Int8Ty);
  allocsize.dump();//test output : i64 ptrtoint (i8* getelementptr (i8* null, i32 1) to i64)
 
  // getTurncOrBitCast : size가 같으면 bitCast,
  // Dest : IntPtrTy(32bit)
  // Src : allocsize(8bit)
  // 왜 하는거지??? 결과 값음???
  /*
	 어떻게 allocsize를  확인할수 잇을까???
	 1)
	 if(IsConstantOne(cast<Value*>(allocsize)))
	 {
		allocsize->dump();
	 } 

	 2)
	 std::cout<<reinterpret_cast<ConstantInt>(allocsize)->isOne();

	 3) 
	 APInt result = cast<Constant>(allocsize)->getUniqueInteger();
	 std::cout<<result.toString(10, true);

	 다 assert crash
  */
  allocsize = ConstantExpr::getTruncOrBitCast(allocsize, IntPtrTy);
	allocsize.dump();//test output : i32 ptrtoint (i8* getelementptr (i8* null, i32 1) to i32)

  // BranF tape 영역 할당
  // 어떻게 만들어지는거지???
  // malloc(type, arraySize) becomes:
  //       bitcast (i8 *malloc(typeSize*arraySize[32])) to type*[IntPtrTy]
  //      Int8Ty :  allocation type
  //      what is the difference between allocsize and arraysize??
  //IR:%malloccall = tail call i8* @malloc(i32 mul (i32 ptrtoint (i8* getelementptr (i8* null, i32 1) to i32), i32 65536))
  ptr_arr = CallInst::CreateMalloc(BB, IntPtrTy, Int8Ty, allocsize, val_mem, 
                                   NULL, "arr");
  // BasicBlock에 malloc 함수 호출을 삽입.
  BB->getInstList().push_back(cast<Instruction>(ptr_arr));

  
  // Tape setting
  // @params i8 *%arr : BrainF tape 위치
  // i8 0 : value (덮어쓸 값)
  // i32 %d : tape의 길이
  // i32 1 : align
  // i1 0 : volatile 여부 ( LLVM memset insturction )
  //call void @llvm.memset.p0i8.i32(i8 *%arr, i8 0, i32 %d, i32 1, i1 0)
  //IR : call void @llvm.memset.p0i8.i32(i8* %malloccall, i8 0, i32 65536, i32 1, i1 false)
  {
    Value *memset_params[] = {
      ptr_arr,
      ConstantInt::get(C, APInt(8, 0)),
      val_mem,
      ConstantInt::get(C, APInt(32, 1)),
      ConstantInt::get(C, APInt(1, 0))
    };

    // memset 함수 호출
    CallInst *memset_call = builder->
      CreateCall(memset_func, memset_params);
    //Tail Call optimization disable
    memset_call->setTailCall(false);
  }

  // arrmax : BrainF tape의 마지막 포인터
  //%arrmax = getelementptr i8 *%arr, i32 %d
  if (comflag & flag_arraybounds) {
    ptr_arrmax = builder->
      CreateGEP(ptr_arr, ConstantInt::get(C, APInt(32, memtotal)), "arrmax");
  }

  // TODO : %head.%d
  // %head : BrainF tape의 중앙 위치
  //%head.%d = getelementptr i8 *%arr, i32 %d
  /*
  getelementptr
  The 'getelementptr' instruction is used to get the address of a subelement of an aggregate data structure.

  %head = getelementptr i8* %malloccall, i32 32768

  ------------------
  |                |
  |                |-
  |--------32kb----- header
  |                |+
  |                |
  |-----------------

  */
  curhead = builder->CreateGEP(ptr_arr,
                               ConstantInt::get(C, APInt(32, memtotal/2)),
                               headreg);



  
  //Function footer

  // brainf 함수의 end 부분
  //brainf.end:
  endbb = BasicBlock::Create(C, label, brainf_func);

  // BrainF tape 영역 해제
  //call free(i8 *%arr)
  endbb->getInstList().push_back(CallInst::CreateFree(ptr_arr, endbb));

  //ret void
  ReturnInst::Create(C, endbb);



  //Error block for array out of bounds
  if (comflag & flag_arraybounds)
  {
    //internal constant : 전역 상수 / String ( String 가져올때는 \
      ConstDataArray::getString 을 쓴다.
    //@aberrormsg = internal constant [%d x i8] c"\00"
    Constant *msg_0 =
      ConstantDataArray::getString(C, "Error: The head has left the tape.",
                                   true);

    // TODO : GlobalVariable ??
    GlobalVariable *aberrormsg = new GlobalVariable(
      *module,
      msg_0->getType(),
      true,
      GlobalValue::InternalLinkage,
      msg_0,
      "aberrormsg");

    // puts 함수 정의
    //declare i32 @puts(i8 *)
    Function *puts_func = cast<Function>(module->
      getOrInsertFunction("puts", IntegerType::getInt32Ty(C),
                      PointerType::getUnqual(IntegerType::getInt8Ty(C)), NULL));

    //brainf.aberror:
    aberrorbb = BasicBlock::Create(C, label, brainf_func);

    //call i32 @puts(i8 *getelementptr([%d x i8] *@aberrormsg, i32 0, i32 0))
    {
      // i32 0
      Constant *zero_32 = Constant::getNullValue(IntegerType::getInt32Ty(C));

      Constant *gep_params[] = {
        zero_32,
        zero_32
      };

      Constant *msgptr = ConstantExpr::
        getGetElementPtr(aberrormsg, gep_params);

      Value *puts_params[] = {
        msgptr
      };

      
      // TODO : 이 함수의 호출 시점은 ?
      // TailCall이 아님.
      CallInst *puts_call =
        CallInst::Create(puts_func,
                         puts_params,
                         "", aberrorbb);
      puts_call->setTailCall(false);
    }

    // brainf.end 로 건너뜀.
    //br label %brainf.end
    BranchInst::Create(endbb, aberrorbb);
  }
}

void BrainF::readloop(PHINode *phi, BasicBlock *oldbb, BasicBlock *testbb,
                      LLVMContext &C) {
  Symbol cursym = SYM_NONE;
  int curvalue = 0;
  Symbol nextsym = SYM_NONE;
  int nextvalue = 0;
  char c;
  int loop;
  int direction;

  while(cursym != SYM_EOF && cursym != SYM_ENDLOOP) {
    // Write out commands
    switch(cursym) {
      case SYM_NONE:
        // Do nothing
        break;

      case SYM_READ:
        {
          //%tape.%d = call i32 @getchar()
          CallInst *getchar_call = builder->CreateCall(getchar_func, tapereg);
          getchar_call->setTailCall(false);
          Value *tape_0 = getchar_call;

          //%tape.%d = trunc i32 %tape.%d to i8
          Value *tape_1 = builder->
            CreateTrunc(tape_0, IntegerType::getInt8Ty(C), tapereg);

          //store i8 %tape.%d, i8 *%head.%d
          builder->CreateStore(tape_1, curhead);
        }
        break;

      case SYM_WRITE:
        {
          //%tape.%d = load i8 *%head.%d
          LoadInst *tape_0 = builder->CreateLoad(curhead, tapereg);

          //%tape.%d = sext i8 %tape.%d to i32
          Value *tape_1 = builder->
            CreateSExt(tape_0, IntegerType::getInt32Ty(C), tapereg);

          //call i32 @putchar(i32 %tape.%d)
          Value *putchar_params[] = {
            tape_1
          };
          CallInst *putchar_call = builder->
            CreateCall(putchar_func,
                       putchar_params);
          putchar_call->setTailCall(false);
        }
        break;

      case SYM_MOVE:
        {
          //%head.%d = getelementptr i8 *%head.%d, i32 %d
          curhead = builder->
            CreateGEP(curhead, ConstantInt::get(C, APInt(32, curvalue)),
                      headreg);

          //Error block for array out of bounds
          if (comflag & flag_arraybounds)
          {
            //%test.%d = icmp uge i8 *%head.%d, %arrmax
            Value *test_0 = builder->
              CreateICmpUGE(curhead, ptr_arrmax, testreg);

            //%test.%d = icmp ult i8 *%head.%d, %arr
            Value *test_1 = builder->
              CreateICmpULT(curhead, ptr_arr, testreg);

            //%test.%d = or i1 %test.%d, %test.%d
            Value *test_2 = builder->
              CreateOr(test_0, test_1, testreg);

            //br i1 %test.%d, label %main.%d, label %main.%d
            BasicBlock *nextbb = BasicBlock::Create(C, label, brainf_func);
            builder->CreateCondBr(test_2, aberrorbb, nextbb);

            //main.%d:
            builder->SetInsertPoint(nextbb);
          }
        }
        break;

      case SYM_CHANGE:
        {
          //%tape.%d = load i8 *%head.%d
          LoadInst *tape_0 = builder->CreateLoad(curhead, tapereg);

          //%tape.%d = add i8 %tape.%d, %d
          Value *tape_1 = builder->
            CreateAdd(tape_0, ConstantInt::get(C, APInt(8, curvalue)), tapereg);

          //store i8 %tape.%d, i8 *%head.%d\n"
          builder->CreateStore(tape_1, curhead);
        }
        break;

      case SYM_LOOP:
        {
          //br label %main.%d
          BasicBlock *testbb = BasicBlock::Create(C, label, brainf_func);
          builder->CreateBr(testbb);

          //main.%d:
          BasicBlock *bb_0 = builder->GetInsertBlock();
          BasicBlock *bb_1 = BasicBlock::Create(C, label, brainf_func);
          builder->SetInsertPoint(bb_1);

          // Make part of PHI instruction now, wait until end of loop to finish
          PHINode *phi_0 =
            PHINode::Create(PointerType::getUnqual(IntegerType::getInt8Ty(C)),
                            2, headreg, testbb);
          phi_0->addIncoming(curhead, bb_0);
          curhead = phi_0;

          readloop(phi_0, bb_1, testbb, C);
        }
        break;

      default:
        std::cerr << "Error: Unknown symbol.\n";
        abort();
        break;
    }

    cursym = nextsym;
    curvalue = nextvalue;
    nextsym = SYM_NONE;

    // Reading stdin loop
    loop = (cursym == SYM_NONE)
        || (cursym == SYM_MOVE)
        || (cursym == SYM_CHANGE);
    while(loop) {
      *in>>c;
      if (in->eof()) {
        if (cursym == SYM_NONE) {
          cursym = SYM_EOF;
        } else {
          nextsym = SYM_EOF;
        }
        loop = 0;
      } else {
        direction = 1;
        switch(c) {
          case '-':
            direction = -1;
            // Fall through

          case '+':
            if (cursym == SYM_CHANGE) {
              curvalue += direction;
              // loop = 1
            } else {
              if (cursym == SYM_NONE) {
                cursym = SYM_CHANGE;
                curvalue = direction;
                // loop = 1
              } else {
                nextsym = SYM_CHANGE;
                nextvalue = direction;
                loop = 0;
              }
            }
            break;

          case '<':
            direction = -1;
            // Fall through

          case '>':
            if (cursym == SYM_MOVE) {
              curvalue += direction;
              // loop = 1
            } else {
              if (cursym == SYM_NONE) {
                cursym = SYM_MOVE;
                curvalue = direction;
                // loop = 1
              } else {
                nextsym = SYM_MOVE;
                nextvalue = direction;
                loop = 0;
              }
            }
            break;

          case ',':
            if (cursym == SYM_NONE) {
              cursym = SYM_READ;
            } else {
              nextsym = SYM_READ;
            }
            loop = 0;
            break;

          case '.':
            if (cursym == SYM_NONE) {
              cursym = SYM_WRITE;
            } else {
              nextsym = SYM_WRITE;
            }
            loop = 0;
            break;

          case '[':
            if (cursym == SYM_NONE) {
              cursym = SYM_LOOP;
            } else {
              nextsym = SYM_LOOP;
            }
            loop = 0;
            break;

          case ']':
            if (cursym == SYM_NONE) {
              cursym = SYM_ENDLOOP;
            } else {
              nextsym = SYM_ENDLOOP;
            }
            loop = 0;
            break;

          // Ignore other characters
          default:
            break;
        }
      }
    }
  }

  if (cursym == SYM_ENDLOOP) {
    if (!phi) {
      std::cerr << "Error: Extra ']'\n";
      abort();
    }

    // Write loop test
    {
      //br label %main.%d
      builder->CreateBr(testbb);

      //main.%d:

      //%head.%d = phi i8 *[%head.%d, %main.%d], [%head.%d, %main.%d]
      //Finish phi made at beginning of loop
      phi->addIncoming(curhead, builder->GetInsertBlock());
      Value *head_0 = phi;

      //%tape.%d = load i8 *%head.%d
      LoadInst *tape_0 = new LoadInst(head_0, tapereg, testbb);

      //%test.%d = icmp eq i8 %tape.%d, 0
      ICmpInst *test_0 = new ICmpInst(*testbb, ICmpInst::ICMP_EQ, tape_0,
                                    ConstantInt::get(C, APInt(8, 0)), testreg);

      //br i1 %test.%d, label %main.%d, label %main.%d
      BasicBlock *bb_0 = BasicBlock::Create(C, label, brainf_func);
      BranchInst::Create(bb_0, oldbb, test_0, testbb);

      //main.%d:
      builder->SetInsertPoint(bb_0);

      //%head.%d = phi i8 *[%head.%d, %main.%d]
      PHINode *phi_1 = builder->
        CreatePHI(PointerType::getUnqual(IntegerType::getInt8Ty(C)), 1,
                  headreg);
      phi_1->addIncoming(head_0, testbb);
      curhead = phi_1;
    }

    return;
  }

  //End of the program, so go to return block
  builder->CreateBr(endbb);

  if (phi) {
    std::cerr << "Error: Missing ']'\n";
    abort();
  }
}
