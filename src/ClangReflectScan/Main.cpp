
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

	crdb::Database indb_bin;
	crdb::ReadBinaryDatabase("output.bin", indb_bin);
	crdb::WriteBinaryDatabase("output2.bin", indb_bin);

	return 0;
}