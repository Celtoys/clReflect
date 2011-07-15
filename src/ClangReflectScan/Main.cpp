
#include "..\ClangReflectTest\crcpp.h"

#include "ClangFrontend.h"
#include "ASTConsumer.h"
#include "ReflectionSpecs.h"

#include "Database.h"
#include "DatabaseTextSerialiser.h"
#include "DatabaseBinarySerialiser.h"


crcpp_reflect(crdb, Field)


int main()
{
	crdb::Database db;
	db.AddBaseTypePrimitives();

	// Parse the AST
	ClangHost clang_host;
	ClangASTParser ast_parser(clang_host);
	ast_parser.ParseAST("../../src/ClangReflectTest/TestReflectionSpecs.cpp");

	clang::ASTContext& ast_context = ast_parser.GetASTContext();

	// Gather reflection specs for the translation unit
	ReflectionSpecs reflection_specs;
	reflection_specs.Gather(ast_context.getTranslationUnitDecl());

	// On the second pass, build the reflection database
	ASTConsumer ast_consumer(ast_context, db, reflection_specs);
	ast_consumer.WalkTranlationUnit(ast_context.getTranslationUnitDecl());

	/*crdb::WriteTextDatabase("output.csv", db);
	crdb::WriteBinaryDatabase("output.bin", db);

	crdb::Database indb_text;
	crdb::ReadTextDatabase("output.csv", indb_text);
	crdb::WriteTextDatabase("output2.csv", indb_text);

	crdb::Database indb_bin;
	crdb::ReadBinaryDatabase("output.bin", indb_bin);
	crdb::WriteBinaryDatabase("output2.bin", indb_bin);*/

	return 0;
}