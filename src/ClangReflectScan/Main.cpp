
#include "ClangReflectScan.h"
#include "ASTConsumer.h"

int main()
{
	ClangReflectScan clrs;
	ASTConsumer ast_consumer;
	clrs.ConsumeAST("../../test/Test.cpp", ast_consumer);

	return 0;
}