#include "llvm/Analysis/Passes.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/PassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/Scalar.h"
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace llvm;

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
enum Token {
  tok_eof = -1,

  // commands
  tok_def = -2, tok_extern = -3,

  // primary
  tok_identifier = -4, tok_number = -5,
  
  // control
  tok_if = -6, tok_then = -7, tok_else = -8,
  tok_for = -9, tok_in = -10
};

static std::string IdentifierStr;  // Filled in if tok_identifier
static double NumVal;              // Filled in if tok_number

/// gettok - Return the next token from standard input.
static int gettok() {
  static int LastChar = ' ';

  // Skip any whitespace.
  while (isspace(LastChar))
    LastChar = getchar();

  if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar())))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def") return tok_def;
    if (IdentifierStr == "extern") return tok_extern;
    if (IdentifierStr == "if") return tok_if;
    if (IdentifierStr == "then") return tok_then;
    if (IdentifierStr == "else") return tok_else;
    if (IdentifierStr == "for") return tok_for;
    if (IdentifierStr == "in") return tok_in;
    return tok_identifier;
  }

  if (isdigit(LastChar) || LastChar == '.') {   // Number: [0-9.]+
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), 0);
    return tok_number;
  }

  if (LastChar == '#') {
    // Comment until end of line.
    do LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');
    
    if (LastChar != EOF)
      return gettok();
  }
  
  // Check for end of file.  Don't eat the EOF.
  if (LastChar == EOF)
    return tok_eof;

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  LastChar = getchar();
  return ThisChar;
}

//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (aka Parse Tree)
//===----------------------------------------------------------------------===//
namespace {
/// ExprAST - Base class for all expression nodes.
class ExprAST {
public:
  virtual ~ExprAST() {}
  virtual Value *Codegen() = 0;
};

/// NumberExprAST - Expression class for numeric literals like "1.0".
class NumberExprAST : public ExprAST {
  double Val;
public:
  NumberExprAST(double val) : Val(val) {}
  virtual Value *Codegen();
};

/// VariableExprAST - Expression class for referencing a variable, like "a".
class VariableExprAST : public ExprAST {
  std::string Name;
public:
  VariableExprAST(const std::string &name) : Name(name) {}
  virtual Value *Codegen();
};

/// BinaryExprAST - Expression class for a binary operator.
class BinaryExprAST : public ExprAST {
  char Op;
  ExprAST *LHS, *RHS;
public:
  BinaryExprAST(char op, ExprAST *lhs, ExprAST *rhs) 
    : Op(op), LHS(lhs), RHS(rhs) {}
  virtual Value *Codegen();
};

/// CallExprAST - Expression class for function calls.
class CallExprAST : public ExprAST {
  std::string Callee;
  std::vector<ExprAST*> Args;
public:
  CallExprAST(const std::string &callee, std::vector<ExprAST*> &args)
    : Callee(callee), Args(args) {}
  virtual Value *Codegen();
};

/// IfExprAST - Expression class for if/then/else.
class IfExprAST : public ExprAST {
  ExprAST *Cond, *Then, *Else;
public:
  IfExprAST(ExprAST *cond, ExprAST *then, ExprAST *_else)
  : Cond(cond), Then(then), Else(_else) {}
  virtual Value *Codegen();
};

/// ForExprAST - Expression class for for/in.
class ForExprAST : public ExprAST {
  std::string VarName;
  ExprAST *Start, *End, *Step, *Body;
public:
  ForExprAST(const std::string &varname, ExprAST *start, ExprAST *end,
             ExprAST *step, ExprAST *body)
    : VarName(varname), Start(start), End(end), Step(step), Body(body) {}
  virtual Value *Codegen();
};

/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
class PrototypeAST {
  std::string Name;
  std::vector<std::string> Args;
public:
  PrototypeAST(const std::string &name, const std::vector<std::string> &args)
    : Name(name), Args(args) {}
  
  Function *Codegen();
};

/// FunctionAST - This class represents a function definition itself.
class FunctionAST {
  PrototypeAST *Proto;
  ExprAST *Body;
public:
  FunctionAST(PrototypeAST *proto, ExprAST *body)
    : Proto(proto), Body(body) {}
  
  Function *Codegen();
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
static int CurTok;
static int getNextToken() {
  return CurTok = gettok();
}

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
static std::map<char, int> BinopPrecedence;

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;
  
  // Make sure it's a declared binop.
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0) return -1;
  return TokPrec;
}

/// Error* - These are little helper functions for error handling.
ExprAST *Error(const char *Str) { fprintf(stderr, "Error: %s\n", Str);return 0;}
PrototypeAST *ErrorP(const char *Str) { Error(Str); return 0; }
FunctionAST *ErrorF(const char *Str) { Error(Str); return 0; }

static ExprAST *ParseExpression();

/// identifierexpr
///   ::= identifier
///   ::= identifier '(' expression* ')'
static ExprAST *ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;
  
  getNextToken();  // eat identifier.
  
  if (CurTok != '(') // Simple variable ref.
    return new VariableExprAST(IdName);
  
  // Call.
  getNextToken();  // eat (
  std::vector<ExprAST*> Args;
  if (CurTok != ')') {
    while (1) {
      ExprAST *Arg = ParseExpression();
      if (!Arg) return 0;
      Args.push_back(Arg);

      if (CurTok == ')') break;

      if (CurTok != ',')
        return Error("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.
  getNextToken();
  
  return new CallExprAST(IdName, Args);
}

/// numberexpr ::= number
static ExprAST *ParseNumberExpr() {
  ExprAST *Result = new NumberExprAST(NumVal);
  getNextToken(); // consume the number
  return Result;
}

/// parenexpr ::= '(' expression ')'
static ExprAST *ParseParenExpr() {
  getNextToken();  // eat (.
  ExprAST *V = ParseExpression();
  if (!V) return 0;
  
  if (CurTok != ')')
    return Error("expected ')'");
  getNextToken();  // eat ).
  return V;
}

/// ifexpr ::= 'if' expression 'then' expression 'else' expression
static ExprAST *ParseIfExpr() {
  getNextToken();  // eat the if.
  
  // condition.
  ExprAST *Cond = ParseExpression();
  if (!Cond) return 0;
  
  if (CurTok != tok_then)
    return Error("expected then");
  getNextToken();  // eat the then
  
  ExprAST *Then = ParseExpression();
  if (Then == 0) return 0;
  
  if (CurTok != tok_else)
    return Error("expected else");
  
  getNextToken();
  
  ExprAST *Else = ParseExpression();
  if (!Else) return 0;
  
  return new IfExprAST(Cond, Then, Else);
}

/// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
static ExprAST *ParseForExpr() {
  getNextToken();  // eat the for.

  if (CurTok != tok_identifier)
    return Error("expected identifier after for");
  
  std::string IdName = IdentifierStr;
  getNextToken();  // eat identifier.
  
  if (CurTok != '=')
    return Error("expected '=' after for");
  getNextToken();  // eat '='.
  
  
  ExprAST *Start = ParseExpression();
  if (Start == 0) return 0;
  if (CurTok != ',')
    return Error("expected ',' after for start value");
  getNextToken();
  
  ExprAST *End = ParseExpression();
  if (End == 0) return 0;
  
  // The step value is optional.
  ExprAST *Step = 0;
  if (CurTok == ',') {
    getNextToken();
    Step = ParseExpression();
    if (Step == 0) return 0;
  }
  
  if (CurTok != tok_in)
    return Error("expected 'in' after for");
  getNextToken();  // eat 'in'.
  
  ExprAST *Body = ParseExpression();
  if (Body == 0) return 0;

  return new ForExprAST(IdName, Start, End, Step, Body);
}

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr
///   ::= ifexpr
///   ::= forexpr
static ExprAST *ParsePrimary() {
  switch (CurTok) {
  default: return Error("unknown token when expecting an expression");
  case tok_identifier: return ParseIdentifierExpr();
  case tok_number:     return ParseNumberExpr();
  case '(':            return ParseParenExpr();
  case tok_if:         return ParseIfExpr();
  case tok_for:        return ParseForExpr();
  }
}

/// binoprhs
///   ::= ('+' primary)*
static ExprAST *ParseBinOpRHS(int ExprPrec, ExprAST *LHS) {
  // If this is a binop, find its precedence.
  while (1) {
    int TokPrec = GetTokPrecedence();
    
    // If this is a binop that binds at least as tightly as the current binop,
    // consume it, otherwise we are done.
    if (TokPrec < ExprPrec)
      return LHS;
    
    // Okay, we know this is a binop.
    int BinOp = CurTok;
    getNextToken();  // eat binop
    
    // Parse the primary expression after the binary operator.
    ExprAST *RHS = ParsePrimary();
    if (!RHS) return 0;
    
    // If BinOp binds less tightly with RHS than the operator after RHS, let
    // the pending operator take RHS as its LHS.
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec+1, RHS);
      if (RHS == 0) return 0;
    }
    
    // Merge LHS/RHS.
    LHS = new BinaryExprAST(BinOp, LHS, RHS);
  }
}

/// expression
///   ::= primary binoprhs
///
static ExprAST *ParseExpression() {
  ExprAST *LHS = ParsePrimary();
  if (!LHS) return 0;
  
  return ParseBinOpRHS(0, LHS);
}

/// prototype
///   ::= id '(' id* ')'
static PrototypeAST *ParsePrototype() {
  if (CurTok != tok_identifier)
    return ErrorP("Expected function name in prototype");

  std::string FnName = IdentifierStr;
  getNextToken();
  
  if (CurTok != '(')
    return ErrorP("Expected '(' in prototype");
  
  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier)
    ArgNames.push_back(IdentifierStr);
  if (CurTok != ')')
    return ErrorP("Expected ')' in prototype");
  
  // success.
  getNextToken();  // eat ')'.
  
  return new PrototypeAST(FnName, ArgNames);
}

/// definition ::= 'def' prototype expression
static FunctionAST *ParseDefinition() {
  getNextToken();  // eat def.
  PrototypeAST *Proto = ParsePrototype();
  if (Proto == 0) return 0;

  if (ExprAST *E = ParseExpression())
    return new FunctionAST(Proto, E);
  return 0;
}

/// toplevelexpr ::= expression
static FunctionAST *ParseTopLevelExpr() {
  if (ExprAST *E = ParseExpression()) {
    // Make an anonymous proto.
    PrototypeAST *Proto = new PrototypeAST("", std::vector<std::string>());
    return new FunctionAST(Proto, E);
  }
  return 0;
}

/// external ::= 'extern' prototype
static PrototypeAST *ParseExtern() {
  getNextToken();  // eat extern.
  return ParsePrototype();
}

//===----------------------------------------------------------------------===//
// Code Generation
//===----------------------------------------------------------------------===//

static Module *TheModule;
static IRBuilder<> Builder(getGlobalContext());
static std::map<std::string, Value*> NamedValues;
static FunctionPassManager *TheFPM;

Value *ErrorV(const char *Str) { Error(Str); return 0; }

Value *NumberExprAST::Codegen() {
  return ConstantFP::get(getGlobalContext(), APFloat(Val));
}

Value *VariableExprAST::Codegen() {
  // Look this variable up in the function.
  Value *V = NamedValues[Name];
  return V ? V : ErrorV("Unknown variable name");
}

Value *BinaryExprAST::Codegen() {
  Value *L = LHS->Codegen();
  Value *R = RHS->Codegen();
  if (L == 0 || R == 0) return 0;
  
  switch (Op) {
  case '+': return Builder.CreateFAdd(L, R, "addtmp");
  case '-': return Builder.CreateFSub(L, R, "subtmp");
  case '*': return Builder.CreateFMul(L, R, "multmp");
  case '<':
    L = Builder.CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    return Builder.CreateUIToFP(L, Type::getDoubleTy(getGlobalContext()),
                                "booltmp");
  default: return ErrorV("invalid binary operator");
  }
}

Value *CallExprAST::Codegen() {
  // Look up the name in the global module table.
  Function *CalleeF = TheModule->getFunction(Callee);
  if (CalleeF == 0)
    return ErrorV("Unknown function referenced");
  
  // If argument mismatch error.
  if (CalleeF->arg_size() != Args.size())
    return ErrorV("Incorrect # arguments passed");

  std::vector<Value*> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->Codegen());
    if (ArgsV.back() == 0) return 0;
  }
  
  return Builder.CreateCall(CalleeF, ArgsV, "calltmp");
}

Value *IfExprAST::Codegen() {
  Value *CondV = Cond->Codegen();
  if (CondV == 0) return 0;
  
  // Convert condition to a bool by comparing equal to 0.0.
  // %ifcond = fcmp one double %x, 0.000000e+00 생성
  CondV = Builder.CreateFCmpONE(CondV, 
                              ConstantFP::get(getGlobalContext(), APFloat(0.0)),
                                "ifcond");
  //부모 함수 객체를 가져온다.
  Function *TheFunction = Builder.GetInsertBlock()->getParent();
  
  // Create blocks for the then and else cases.  Insert the 'then' block at the
  // end of the function.
  //부모 함수 TheFunction의 Basic Block List에 생성한 TheBB Basic Block을 추가한다.
  //이름 설정.
  BasicBlock *ThenBB = BasicBlock::Create(getGlobalContext(), "then", TheFunction); // (1)
  //이름 설정만 한다.
  BasicBlock *ElseBB = BasicBlock::Create(getGlobalContext(), "else"); // (2)
  BasicBlock *MergeBB = BasicBlock::Create(getGlobalContext(), "ifcont"); // (3)
 
  // br i1 %ifcond, label %then, label %else 생성
  Builder.CreateCondBr(CondV, ThenBB, ElseBB); // (4)
  
  // Emit then value.
  // Builder의 BB와 InsertPtr을 ThenBB, ThenBB->end()로 설정
  Builder.SetInsertPoint(ThenBB); // (5)
  
  //Then Expression 코드 생성. 
  Value *ThenV = Then->Codegen(); // (6)
  if (ThenV == 0) return 0;
  
  // br label %ifcont 생성.
  Builder.CreateBr(MergeBB); // (7)
  // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
  // Then->Codegen(); 시 ThenBB에 속하는 하위 Basic Block 이 생길수 있기때문에 
  // 즉, IfExprAST::Codegen 가 재귀적으로 불릴수 있기때문에 IfExprAST::ThenBB가 변경될수 있다.
  // 이후 같은 레벨의 코드로 돌아오면 ThenBB를 원복해야 하기 때문에  Builder.GetInsertBlock()
  // 로 다시 업데이트 해준다.
  // 즉, ThenBB에는 가장 깊은 Depth의 BasicBlock의 첫 부분을 포인팅하게 된다.
  ThenBB = Builder.GetInsertBlock(); // (8)
  
  // Emit else block.
  // TheFunction의  Basic Block List에 push_back
  TheFunction->getBasicBlockList().push_back(ElseBB); // (9)
  // Builder의 BB와 InsertPtr을 각각 ElseBB, ElseBB->end()로 설정.
  Builder.SetInsertPoint(ElseBB); // (10)
 
  //Else expression 코드 생성 
  Value *ElseV = Else->Codegen(); // (11)
  if (ElseV == 0) return 0;
  
  //br label %ifcont 생성
  Builder.CreateBr(MergeBB); // (12)

  // Codegen of 'Else' can change the current block, update ElseBB for the PHI.
  //line:549 ThenBB = Builder.GetInsertBlock() 와 같은 경우 인듯??? 
  ElseBB = Builder.GetInsertBlock(); // (13)
  
  // Emit merge block.
  // TheFunction의  Basic Block List에 push_back
  TheFunction->getBasicBlockList().push_back(MergeBB); // (14)
  // Builder의 BB와 InsertPtr을 각각 MergeBB, MergeBB->end()로 설정.
  Builder.SetInsertPoint(MergeBB); // (15)
  
  //%iftmp = phi double [ %calltmp, %then ], [ %calltmp1, %else ]
  PHINode *PN = Builder.CreatePHI(Type::getDoubleTy(getGlobalContext()), 2,
                                  "iftmp"); // (16)
  PN->addIncoming(ThenV, ThenBB); // (17)
  PN->addIncoming(ElseV, ElseBB); // (18)

  /***
	TheFunction
	[br CondV, ThenBB, ElseBB] (4)
		| (1)
	ThenBB (1)   <--- InsertPoint (5),  ---> GetInsertBlock(8)
	[Then->codegen()] (6)
	[br MergeBB] (7)
		| (9)
	ElseBB (2)   <--- InsertPoint (10), ---> GetInsertBlock (13)
	[Else->codegen()] (11)
	[br MergeBB] (12) 
		| (14)
	MergeBB (3)  <--- InsertPoint (15)
	[PN*] (16)
		[phi (ThenV, ThenBB), (ElseV, ElseBB)] (17, 18)

  (8, 13)번 코드는, 중첩 BasicBlock인 경우 가장 깊은 Depth의 BasicBlock을 가리키도록 한다.
  ***/
  return PN;
}

Value *ForExprAST::Codegen() {
  // Output this as:
  //   ...
  //   start = startexpr
  //   goto loop
  // loop: 
  //   variable = phi [start, loopheader], [nextvariable, loopend]
  //   ...
  //   bodyexpr
  //   ...
  // loopend:
  //   step = stepexpr
  //   nextvariable = variable + step
  //   endcond = endexpr
  //   br endcond, loop, endloop
  // outloop:

/*
extern putchard(char)
def printstar(n)
  for i = (start expr), i < n, 1.0 in
    putchard(42);  # ascii 42 = '*'

# print 100 '*' characters
printstar(100);
*/
  // Emit the start code first, without 'variable' in scope.
  // (start expr) 에 대한 코드 생성
  Value *StartVal = Start->Codegen();
  if (StartVal == 0) return 0;
  
  // Make the new basic block for the loop header, inserting after current
  // block.
  Function *TheFunction = Builder.GetInsertBlock()->getParent();
  //현재 설정된 Basic Block을 PreheaderBB 저장 
  BasicBlock *PreheaderBB = Builder.GetInsertBlock();
  //loop Basic Block에 생성해서 TheFunction의 Basic Block List추가
  BasicBlock *LoopBB = BasicBlock::Create(getGlobalContext(), "loop", TheFunction);
  
  // Insert an explicit fall through from the current block to the LoopBB.
  // br label %loop 생성
  Builder.CreateBr(LoopBB);

  // Start insertion in LoopBB.
  Builder.SetInsertPoint(LoopBB);
  
  // Start the PHI node with an entry for Start.
  PHINode *Variable = Builder.CreatePHI(Type::getDoubleTy(getGlobalContext()), 2, VarName.c_str());
  //caller 컨트롤블록이 PreheaderBB 일 경우 i에 StartVal 저장.
  Variable->addIncoming(StartVal, PreheaderBB);
  
  // Within the loop, the variable is defined equal to the PHI node.  If it
  // shadows an existing variable, we have to restore it, so save it now.
  // 같은 이름의 변수가 이전에 쓰일수 있으므로 이전 변수를 임시 보관
  Value *OldVal = NamedValues[VarName];
  NamedValues[VarName] = Variable;
  
  // Emit the body of the loop.  This, like any other expr, can change the
  // current BB.  Note that we ignore the value computed by the body, but don't
  // allow an error.
  //%calltmp = call double @putchard(double 4.200000e+01) 생성
  if (Body->Codegen() == 0)
    return 0;
  
  // Emit the step value.
  // Step 값 계산 없을 경우 1.0로 설정
  Value *StepVal;
  if (Step) {
    StepVal = Step->Codegen();
    if (StepVal == 0) return 0;
  } else {
    // If not specified, use 1.0.
    StepVal = ConstantFP::get(getGlobalContext(), APFloat(1.0));
  }
  
  // %nextvar = fadd double %i, 1.000000e+00 생성
  Value *NextVar = Builder.CreateFAdd(Variable, StepVal, "nextvar");

  // Compute the end condition.
  //  %cmptmp = fcmp ult double %i, %n 생성
  Value *EndCond = End->Codegen();
  if (EndCond == 0) return EndCond;
  
  // Convert condition to a bool by comparing equal to 0.0.
  // %loopcond = fcmp one double %booltmp, 0.000000e+00
  EndCond = Builder.CreateFCmpONE(EndCond, 
                              ConstantFP::get(getGlobalContext(), APFloat(0.0)),
                                  "loopcond");
  
  // Create the "after loop" block and insert it.
  //  LoopBB안에 또다른 Basic Block 이 생길수 있으므로 이에대한 업데이트
  BasicBlock *LoopEndBB = Builder.GetInsertBlock();
  // afterloop Basic Block 생성
  BasicBlock *AfterBB = BasicBlock::Create(getGlobalContext(), "afterloop", TheFunction);
  
  //  br i1 %loopcond, label %loop, label %afterloop 생성
  // Insert the conditional branch into the end of LoopEndBB.
  Builder.CreateCondBr(EndCond, LoopBB, AfterBB);
  
  // Any new code will be inserted in AfterBB.
  Builder.SetInsertPoint(AfterBB);
  
  // Add a new entry to the PHI node for the backedge.
  // %i = phi double [ 1.000000e+00, %entry ], [ %nextvar, %loop ]
  // 에서 [ %nextvar, %loop ] 조건 생성
  Variable->addIncoming(NextVar, LoopEndBB);
  
  // Restore the unshadowed variable.
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  
  // for expr always returns 0.0.
  return Constant::getNullValue(Type::getDoubleTy(getGlobalContext()));
}

Function *PrototypeAST::Codegen() {
  // Make the function type:  double(double,double) etc.
  std::vector<Type*> Doubles(Args.size(),
                             Type::getDoubleTy(getGlobalContext()));
  FunctionType *FT = FunctionType::get(Type::getDoubleTy(getGlobalContext()),
                                       Doubles, false);
  
  Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule);
  
  // If F conflicted, there was already something named 'Name'.  If it has a
  // body, don't allow redefinition or reextern.
  if (F->getName() != Name) {
    // Delete the one we just made and get the existing one.
    F->eraseFromParent();
    F = TheModule->getFunction(Name);
    
    // If F already has a body, reject this.
    if (!F->empty()) {
      ErrorF("redefinition of function");
      return 0;
    }
    
    // If F took a different number of args, reject.
    if (F->arg_size() != Args.size()) {
      ErrorF("redefinition of function with different # args");
      return 0;
    }
  }
  
  // Set names for all arguments.
  unsigned Idx = 0;
  for (Function::arg_iterator AI = F->arg_begin(); Idx != Args.size();
       ++AI, ++Idx) {
    AI->setName(Args[Idx]);
    
    // Add arguments to variable symbol table.
    NamedValues[Args[Idx]] = AI;
  }
  
  return F;
}

Function *FunctionAST::Codegen() {
  NamedValues.clear();
  
  Function *TheFunction = Proto->Codegen();
  if (TheFunction == 0)
    return 0;
  
  // Create a new basic block to start insertion into.
  BasicBlock *BB = BasicBlock::Create(getGlobalContext(), "entry", TheFunction);
  Builder.SetInsertPoint(BB);
  
  if (Value *RetVal = Body->Codegen()) {
    // Finish off the function.
    Builder.CreateRet(RetVal);

    // Validate the generated code, checking for consistency.
    verifyFunction(*TheFunction);

    // Optimize the function.
    TheFPM->run(*TheFunction);
    
    return TheFunction;
  }
  
  // Error reading body, remove function.
  TheFunction->eraseFromParent();
  return 0;
}

//===----------------------------------------------------------------------===//
// Top-Level parsing and JIT Driver
//===----------------------------------------------------------------------===//

static ExecutionEngine *TheExecutionEngine;

static void HandleDefinition() {
  if (FunctionAST *F = ParseDefinition()) {
    if (Function *LF = F->Codegen()) {
      fprintf(stderr, "Read function definition:");
      LF->dump();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleExtern() {
  if (PrototypeAST *P = ParseExtern()) {
    if (Function *F = P->Codegen()) {
      fprintf(stderr, "Read extern: ");
      F->dump();
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

static void HandleTopLevelExpression() {
  // Evaluate a top-level expression into an anonymous function.
  if (FunctionAST *F = ParseTopLevelExpr()) {
    if (Function *LF = F->Codegen()) {
      // JIT the function, returning a function pointer.
      void *FPtr = TheExecutionEngine->getPointerToFunction(LF);
      
      // Cast it to the right type (takes no arguments, returns a double) so we
      // can call it as a native function.
      double (*FP)() = (double (*)())(intptr_t)FPtr;
      fprintf(stderr, "Evaluated to %f\n", FP());
    }
  } else {
    // Skip token for error recovery.
    getNextToken();
  }
}

/// top ::= definition | external | expression | ';'
static void MainLoop() {
  while (1) {
    fprintf(stderr, "ready> ");
    switch (CurTok) {
    case tok_eof:    return;
    case ';':        getNextToken(); break;  // ignore top-level semicolons.
    case tok_def:    HandleDefinition(); break;
    case tok_extern: HandleExtern(); break;
    default:         HandleTopLevelExpression(); break;
    }
  }
}

//===----------------------------------------------------------------------===//
// "Library" functions that can be "extern'd" from user code.
//===----------------------------------------------------------------------===//

/// putchard - putchar that takes a double and returns 0.
extern "C" 
double putchard(double X) {
  putchar((char)X);
  return 0;
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main() {
  InitializeNativeTarget();
  LLVMContext &Context = getGlobalContext();

  // Install standard binary operators.
  // 1 is lowest precedence.
  BinopPrecedence['<'] = 10;
  BinopPrecedence['+'] = 20;
  BinopPrecedence['-'] = 20;
  BinopPrecedence['*'] = 40;  // highest.

  // Prime the first token.
  fprintf(stderr, "ready> ");
  getNextToken();

  // Make the module, which holds all the code.
  TheModule = new Module("my cool jit", Context);

  // Create the JIT.  This takes ownership of the module.
  std::string ErrStr;
  TheExecutionEngine = EngineBuilder(TheModule).setErrorStr(&ErrStr).create();
  if (!TheExecutionEngine) {
    fprintf(stderr, "Could not create ExecutionEngine: %s\n", ErrStr.c_str());
    exit(1);
  }

  FunctionPassManager OurFPM(TheModule);

  // Set up the optimizer pipeline.  Start with registering info about how the
  // target lays out data structures.
  TheModule->setDataLayout(TheExecutionEngine->getDataLayout());
  OurFPM.add(new DataLayoutPass(TheModule));
  // Provide basic AliasAnalysis support for GVN.
  OurFPM.add(createBasicAliasAnalysisPass());
  // Do simple "peephole" optimizations and bit-twiddling optzns.
  OurFPM.add(createInstructionCombiningPass());
  // Reassociate expressions.
  OurFPM.add(createReassociatePass());
  // Eliminate Common SubExpressions.
  OurFPM.add(createGVNPass());
  // Simplify the control flow graph (deleting unreachable blocks, etc).
  OurFPM.add(createCFGSimplificationPass());

  OurFPM.doInitialization();

  // Set the global so the code gen can use this.
  TheFPM = &OurFPM;

  // Run the main "interpreter loop" now.
  MainLoop();

  TheFPM = 0;

  // Print out all of the generated code.
  TheModule->dump();

  return 0;
}
