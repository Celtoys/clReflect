
#include "ClangReflectScan.h"
#include "Database.h"
#include "DatabaseTextSerialiser.h"
#include "DatabaseBinarySerialiser.h"

int main()
{
	crdb::Database db;
	db.AddBaseTypePrimitives();

	ClangReflectScan clrs;
	clrs.ConsumeAST("../../test/Test.cpp", db);

	crdb::WriteTextDatabase("output.csv", db);
	crdb::WriteBinaryDatabase("output.bin", db);

	crdb::Database indb_text;
	crdb::ReadTextDatabase("output.csv", indb_text);
	crdb::WriteTextDatabase("output2.csv", indb_text);

	return 0;
}