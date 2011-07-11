
#include "ClangReflectScan.h"

int main()
{
	ClangReflectScan clrs;
	clrs.ConsumeAST("../../test/Test.cpp");

	return 0;
}