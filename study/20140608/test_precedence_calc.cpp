#include <iostream>
#include <map>

/*
Author : eundoo.song

This simple caculator provides an example of Operator-precedence_parser.
reference : 1) http://en.wikipedia.org/wiki/Operator-precedence_parser
			2) http://llvm.org/docs/tutorial/LangImpl2.html

But this is Not fully implemented caculator, meaning that it runs as very limited one.
1. It can't parse any number that has more than two digit number or any sub-expression.
   So, all operands must be [0-9]
   1+2*4-2 --> OK
   1+22*44-2 --> NO!!
2. Only "+, -, *, /" supported.
3. It doesn't handle all possible errors.

Please improve me! :)

*/
class calc
{
private:
	int CurTokIdx;// array index of Current Token
	std::map<char, int> op_precedence;
	std::string Input;

	int binaryCalc(int exprPrec, int LHS)
	{
		while(1)
		{
			int TokPrec = GetOpPrecedence();

			if(TokPrec < exprPrec)
			{
				return LHS;
			}

			char OP = Input[CurTokIdx++];
			int RHS = Input[CurTokIdx++] - 48;

			int NextPrec = GetOpPrecedence();
			if(TokPrec < NextPrec)
			{
				RHS = binaryCalc(TokPrec+1, RHS);			
			} 

			std::cout<<"RHS:"<<RHS<<std::endl; 
			std::cout<<"OP:"<<OP<<std::endl; 
			std::cout<<"LHS:"<<LHS<<"\n"<<std::endl; 

			LHS = DoCalc(OP,LHS,RHS);
 
		}
	}

	int DoCalc(char OP, int LHS, int RHS)
	{
		switch(OP)
		{
			case '+':
				return LHS + RHS;		
			case '-':
				return LHS - RHS;		
			case '*':
				return LHS * RHS;		
			case '/':
				return LHS / RHS;
			defualt:
				return -1;		
		}					
	}

	int GetOpPrecedence()
	{
		if(CurTokIdx >= Input.size())
		{
			return -1;
		}

		if(op_precedence[Input[CurTokIdx]] > 0)
		{
			return op_precedence[Input[CurTokIdx]]; 
		}

		return -1;
	}

public:
	calc()
	{
		op_precedence['+'] =  20;
		op_precedence['-'] =  20;
		op_precedence['*'] =  30;
		op_precedence['/'] =  30;
	}

	int DoCalc(const std::string& input)
	{
		Input = input;	
		CurTokIdx = 1;
		binaryCalc(0, input[0] - 48);
	}
};


int main(void)
{
	std::string input;
	std::cin>>input;

	calc c;
	std::cout<<c.DoCalc(input)<<std::endl;
	return 0;
}

