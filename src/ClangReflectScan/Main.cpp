
#include "ClangFrontend.h"
#include "ASTConsumer.h"

#include "Database.h"
#include "DatabaseTextSerialiser.h"
#include "DatabaseBinarySerialiser.h"

int main()
{
	crdb::Database db;
	db.AddBaseTypePrimitives();

	ClangHost clang_host;
	ClangASTParser ast_parser(clang_host);
	ast_parser.ParseAST("../../test/Test.cpp");

	clang::ASTContext& ast_context = ast_parser.GetASTContext();
	ASTConsumer ast_consumer(ast_context, db);
	ast_consumer.WalkTranlationUnit(ast_context.getTranslationUnitDecl());

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