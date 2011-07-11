
#include "ClangReflectScan.h"
#include "Database.h"
#include "DatabaseTextSerialiser.h"

int main()
{
	crdb::Database db;

	ClangReflectScan clrs;
	clrs.ConsumeAST("../../test/Test.cpp", db);

	crdb::WriteTextDatabase("output.csv", db);

	return 0;
}