
#include "..\ClangReflectTest\crcpp.h"

#include "ClangFrontend.h"
#include "ASTConsumer.h"
#include "ReflectionSpecs.h"
#include "Arguments.h"

#include "Database.h"
#include "DatabaseTextSerialiser.h"
#include "DatabaseBinarySerialiser.h"


bool FileExists(const char* filename)
{
	// For now, just try to open the file
	FILE* fp = fopen(filename, "r");
	if (fp == 0)
	{
		fclose(fp);
		return false;
	}
	fclose(fp);
	return true;
}


int main(int argc, const char* argv[])
{
	// Leave early if there aren't enough arguments
	Arguments args(argc, argv);
	if (args.Count() < 2)
	{
		printf("Not enough arguments\n");
		return 1;
	}

	// Does the input file exist?
	const char* input_filename = argv[1];
	if (!FileExists(input_filename))
	{
		printf("Couldn't find the input file %s\n", input_filename);
		return 1;
	}

	// Parse the AST
	ClangHost clang_host;
	ClangASTParser ast_parser(clang_host);
	ast_parser.ParseAST(input_filename);

	// Gather reflection specs for the translation unit
	clang::ASTContext& ast_context = ast_parser.GetASTContext();
	ReflectionSpecs reflection_specs(args.Have("-reflect_specs_all"));
	reflection_specs.Gather(ast_context.getTranslationUnitDecl());

	// On the second pass, build the reflection database
	crdb::Database db;
	db.AddBaseTypePrimitives();
	ASTConsumer ast_consumer(ast_context, db, reflection_specs);
	ast_consumer.WalkTranlationUnit(ast_context.getTranslationUnitDecl());

	// Write any reflection specs to file so that the next stage in the compilation
	// sequence can pick them up and instruct the compiler to report layout data
	std::string reflect_specs_file = args.GetProperty("-reflect_specs");
	if (reflect_specs_file != "")
	{
		reflection_specs.Write(reflect_specs_file.c_str(), db);
	}

	if (args.Have("-test"))
	{
		crdb::WriteTextDatabase("output.csv", db);
		crdb::WriteBinaryDatabase("output.bin", db);

		crdb::Database indb_text;
		crdb::ReadTextDatabase("output.csv", indb_text);
		crdb::WriteTextDatabase("output2.csv", indb_text);

		crdb::Database indb_bin;
		crdb::ReadBinaryDatabase("output.bin", indb_bin);
		crdb::WriteBinaryDatabase("output2.bin", indb_bin);
	}

	return 0;
}