//===--- examples/Fibonacci/fibonacci.cpp - An example use of the JIT -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This small program provides an example of how to build quickly a small module
// with function Fibonacci and execute it with the JIT.
//
// The goal of this snippet is to create in the memory the LLVM module
// consisting of one function as follow:
//
//   int fib(int x) {
//     if(x<=2) return 1;
//     return fib(x-1)+fib(x-2);
//   }
//
// Once we have this, we compile the module via JIT, then execute the `fib'
// function and return result to a driver, i.e. to a "host program".
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Verifier.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

static Function *CreateFactorialFunction(Module *M, LLVMContext &Context) {
    // Create the fib function and insert it into module M. This function is said
    // to return an int and take an int parameter.
    
    // int factorial( int )
    Function *FactorialF =
    cast<Function>(M->getOrInsertFunction("factorial", Type::getInt32Ty(Context),
                                          Type::getInt32Ty(Context),
                                          (Type *)0));
    
    // Add a basic block to the function.
    BasicBlock *BB = BasicBlock::Create(Context, "EntryBlock", FactorialF);
    
    // Get pointers to the constants.
    Value *One = ConstantInt::get(Type::getInt32Ty(Context), 1);
    
    // Get pointer to the integer argument of the add1 function...
    Argument *ArgX = FactorialF->arg_begin();   // Get the arg.
    
    // Create the Return block.
    BasicBlock *RetBB = BasicBlock::Create(Context, "ReturnBlock", FactorialF);
    // Create an Recurse block.
    BasicBlock* RecBB = BasicBlock::Create(Context, "RecurseBlock", FactorialF);
    
    // Create the "if ( arg < 1 ) goto RetBB"
    Value *CondInst = new ICmpInst(*BB, ICmpInst::ICMP_SLT, ArgX, One, "cond");
    BranchInst::Create(RetBB, RecBB, CondInst, BB);
    
    // Create: ret int 1
    ReturnInst::Create(Context, One, RetBB);
    
    // create fac(n-1)
    Value *Sub = BinaryOperator::CreateSub(ArgX, One, "arg", RecBB);
    CallInst *callFactorial = CallInst::Create(FactorialF, Sub, "fact1", RecBB);
    callFactorial->setTailCall();
    
    // n * fac( n - 1)
    Value *Mul = BinaryOperator::CreateMul(ArgX, callFactorial,
                                           "mulresult", RecBB);
    
    // Create the return instruction and add it to the basic block
    ReturnInst::Create(Context, Mul, RecBB);
    
    return FactorialF;
}


int main(int argc, char **argv) {
    int n = argc > 1 ? atol(argv[1]) : 10;
    
    InitializeNativeTarget();
    LLVMContext Context;
    
    // Create some module to put our function into it.
    std::unique_ptr<Module> M(new Module("test", Context));
    
    // We are about to create the "fib" function:
    Function *FactorialF = CreateFactorialFunction(M.get(), Context);
    
    // Now we going to create JIT
    std::string errStr;
    ExecutionEngine *EE =
    EngineBuilder(M.get())
    .setErrorStr(&errStr)
    .setEngineKind(EngineKind::JIT)
    .create();
    
    if (!EE) {
        errs() << argv[0] << ": Failed to construct ExecutionEngine: " << errStr
        << "\n";
        return 1;
    }
    
    errs() << "verifying... ";
    if (verifyModule(*M)) {
        errs() << argv[0] << ": Error constructing function!\n";
        return 1;
    }
    
    errs() << "OK\n";
    errs() << "We just constructed this LLVM module:\n\n---------\n" << *M;
    errs() << "---------\nstarting factorial(" << n << ") with JIT...\n";
    
    // Call the Factorial function with argument n:
    std::vector<GenericValue> Args(1);
    Args[0].IntVal = APInt(32, n);
    GenericValue GV = EE->runFunction(FactorialF, Args);
    
    // import result of execution
    outs() << "Result: " << GV.IntVal << "\n";
    
    return 0;
}



