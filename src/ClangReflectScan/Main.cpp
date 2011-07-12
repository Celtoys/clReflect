
#include "ClangReflectScan.h"
#include "Database.h"
#include "DatabaseTextSerialiser.h"

int main()
{
	crdb::Database db;
	db.AddBaseTypePrimitives();

	ClangReflectScan clrs;
	clrs.ConsumeAST("../../test/Test.cpp", db);

	crdb::WriteTextDatabase("output.csv", db);

	crdb::Database indb;
	crdb::ReadTextDatabase("output.csv", indb);
	crdb::WriteTextDatabase("output2.csv", indb);

	return 0;
}