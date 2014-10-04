//===-- examples/HowToUseJIT/HowToUseJIT.cpp - An example use of the JIT --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This small program provides an example of how to quickly build a small
//  module with two functions and execute it with the JIT.
//
// Goal:
//  The goal of this snippet is to create in the memory
//  the LLVM module consisting of two functions as follow: 
//
// int add1(int x) {
//   return x+1;
// }
//
// int foo() {
//   return add1(10);
// }
//
// then compile the module via JIT, then execute the `foo'
// function and return result to a driver, i.e. to a "host program".
//
// Some remarks and questions:
//
// - could we invoke some code using noname functions too?
//   e.g. evaluate "foo()+foo()" without fears to introduce
//   conflict of temporary function name with some real
//   existing function name?
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

int main() {

/** 20140510
 * naitive target 초기화
 */
  InitializeNativeTarget();

/** 20140510
 * compiler 생성시  정보를 담고 있는 context 초기화
 */
  LLVMContext Context;
  
  // Create some module to put our function into it.
/** 20140510
 * 생성된 context로 module을 이름(test)과 함께 생성
 */
  Module *M = new Module("test", Context);

  // Create the add1 function entry and insert this entry into module M.  The
  // function will have a return type of "int" and take an argument of "int".
  // The '0' terminates the list of argument types.
  /** 20140510
   *  add1 함수를 생성해서 Add1F:Function*에 대입
   *  20141004
   *  int add1( int ) : 첫 번째 인자 값을 받아 1 더해서 return 해주는 함수.
   */
  Function *Add1F =
    cast<Function>(M->getOrInsertFunction("add1", Type::getInt32Ty(Context),
                                          Type::getInt32Ty(Context),
                                          (Type *)0));

  // Add a basic block to the function. As before, it automatically inserts
  // because of the last argument.
  /** 20140510
   * BackBlock을 하나 생성해서 BB:BasicBlock*에 대입
   * (Add1F 끝에 basic block이 추가되는듯?? Create 함수 주석 참고)
   */
  BasicBlock *BB = BasicBlock::Create(Context, "EntryBlock", Add1F);

  // Create a basic block builder with default parameters.  The builder will
  // automatically append instructions to the basic block `BB'.
  /** 20140510
   * 생성한 Basic Block을 가지고 IR 을 생성하고 정의된 위치에 삽입 (삽입되는 위치는
   * 따로 조건이 있는듯???)
   */
  IRBuilder<> builder(BB);

  /** 20140510
   * 1. 1에 대한 Value를만들어서 (buider.getInt32)
   * 2. add1의 인자준비.
   * 3. Value 1과 인자를 Add하는IR 생성하고 그 결과 값을 Add 에 저장 (builder.CreateAdd(One, ArgX))
   * 4. Add를 리틴값으로 생성(builder.CreateRet(Add))
   *
   */
  // Get pointers to the constant `1'.
  Value *One = builder.getInt32(1);

  // Get pointers to the integer argument of the add1 function...
  assert(Add1F->arg_begin() != Add1F->arg_end()); // Make sure there's an arg
  Argument *ArgX = Add1F->arg_begin();  // Get the arg
  ArgX->setName("AnArg");            // Give it a nice symbolic name for fun.

  // Create the add instruction, inserting it into the end of BB.
  Value *Add = builder.CreateAdd(One, ArgX);

  // Create the return instruction and add it to the basic block
  builder.CreateRet(Add);

  // Now, function add1 is ready.


  // Now we're going to create function `foo', which returns an int and takes no
  // arguments.
  /** 20140510
   * foo 함수 생성해서 FooF:Function*에 할당.
   *  20141004
   *  int foo( void )
   */
  Function *FooF =
    cast<Function>(M->getOrInsertFunction("foo", Type::getInt32Ty(Context),
                                          (Type *)0));

  /** 20140510
   * foo함수가 가지는 Basic Block생성.
   */
  // Add a basic block to the FooF function.
  BB = BasicBlock::Create(Context, "EntryBlock", FooF);

  // Tell the basic block builder to attach itself to the new basic block
  /** 20140510
   * basic block에 지정하고 instruction(IR)이 append될 위치 지정.`
   */
  builder.SetInsertPoint(BB);

  // Get pointer to the constant `10'.
  Value *Ten = builder.getInt32(10);

  // Pass Ten to the call to Add1F
  CallInst *Add1CallRes = builder.CreateCall(Add1F, Ten);
  
  /** 20140510
   * Tail Call : http://en.wikipedia.org/wiki/Tail_call
   * 20141004
   * return add1( 10 );
   */
  Add1CallRes->setTailCall(true);

  // Create the return instruction and add it to the basic block.
  builder.CreateRet(Add1CallRes);

  // Now we create the JIT.
  /** 20140510
   * 생성한 Module을 사용해서 JIT Engine Builder를 생성후 EE::ExecutionEngine*에 저장
   */
  ExecutionEngine* EE = EngineBuilder(M).create();

  outs() << "We just constructed this LLVM module:\n\n" << *M;
  outs() << "\n\nRunning foo: ";
  outs().flush();

  // Call the `foo' function with no arguments:
  std::vector<GenericValue> noargs;
  /** 20140510
   * runFunction의 종류 : 인터프리터, JIT, MC JIT
   * About MC JIT
   *	- http://blog.llvm.org/2010/04/intro-to-llvm-mc-project.html
   * 
   * 메인 함수인 Foo 를 인자로 함수 JIT으로 실행
   */
  GenericValue gv = EE->runFunction(FooF, noargs);

  // Import result of execution:
  outs() << "Result: " << gv.IntVal << "\n";
  /** 20140510
   * JIT 리소스 정리
   */
  EE->freeMachineCodeForFunction(FooF);
  delete EE;
  /** 20140510
   * llvm shutdown
   */
  llvm_shutdown();
  return 0;
}
