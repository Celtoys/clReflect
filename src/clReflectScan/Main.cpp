
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include "ClangFrontend.h"
#include "ASTConsumer.h"
#include "ReflectionSpecs.h"

#include "clReflectCore/Arguments.h"
#include "clReflectCore/Logging.h"
#include "clReflectCore/Database.h"
#include "clReflectCore/DatabaseTextSerialiser.h"
#include "clReflectCore/DatabaseBinarySerialiser.h"

#include "clang/AST/ASTContext.h"

#include <stdio.h>
#include <time.h>


namespace
{
	bool FileExists(const char* filename)
	{
		// For now, just try to open the file
		FILE* fp = fopen(filename, "r");
		if (fp == 0)
		{
			return false;
		}
		fclose(fp);
		return true;
	}

	
	bool EndsWith(const std::string& str, const std::string& end)
	{
		return str.rfind(end) == str.length() - end.length();
	}

	void WriteIncludedHeaders(const ClangParser& ast_parser, const char* outputfile, const char* input_filename)
	{
		std::vector< std::pair<ClangParser::HeaderType, std::string> > header_files;
		ast_parser.GetIncludedFiles(header_files);

		FILE* fp = fopen(outputfile, "wt");
		// Print to output, noting that the source file will also be in the list
		for (size_t i = 0; i < header_files.size(); i++)
		{
			if (header_files[i].second != input_filename)
			{
				fprintf(fp, "%c %s",
					(header_files[i].first==ClangParser::HeaderType_User) ? 'u' : ( (header_files[i].first==ClangParser::HeaderType_System) ? 's' : 'e'),
					header_files[i].second.c_str());
				fprintf(fp, "\n");
			}
		}

		fclose(fp);
	}


	void WriteDatabase(const cldb::Database& db, const std::string& filename)
	{
		if (EndsWith(filename, ".csv"))
		{
			cldb::WriteTextDatabase(filename.c_str(), db);
		}
		else
		{
			cldb::WriteBinaryDatabase(filename.c_str(), db);
		}
	}


	void TestDBReadWrite(const cldb::Database& db)
	{
		cldb::WriteTextDatabase("output.csv", db);
		cldb::WriteBinaryDatabase("output.bin", db);

		cldb::Database indb_text;
		cldb::ReadTextDatabase("output.csv", indb_text);
		cldb::WriteTextDatabase("output2.csv", indb_text);

		cldb::Database indb_bin;
		cldb::ReadBinaryDatabase("output.bin", indb_bin);
		cldb::WriteBinaryDatabase("output2.bin", indb_bin);
	}
}


int main(int argc, const char* argv[])
{
	float start = clock();

	LOG_TO_STDOUT(main, ALL);

	// Leave early if there aren't enough arguments
	Arguments args(argc, argv);
	if (args.Count() < 2)
	{
		LOG(main, ERROR, "Not enough arguments\n");
		return 1;
	}

	// Does the input file exist?
	const char* input_filename = argv[1];
	if (!FileExists(input_filename))
	{
		LOG(main, ERROR, "Couldn't find the input file %s\n", input_filename);
		return 1;
	}

	float prologue = clock();

	// Parse the AST
	ClangParser parser(args);
	if (!parser.ParseAST(input_filename))
	{
		LOG(main, ERROR, "Errors parsing the AST\n");
		return 1;
	}

	float parsing = clock();

	// Gather reflection specs for the translation unit
	clang::ASTContext& ast_context = parser.GetASTContext();
	std::string spec_log = args.GetProperty("-spec_log");
	ReflectionSpecs reflection_specs(args.Have("-reflect_specs_all"), spec_log);
	reflection_specs.Gather(ast_context.getTranslationUnitDecl());

	float specs = clock();

	// On the second pass, build the reflection database
	cldb::Database db;
	db.AddBaseTypePrimitives();
	std::string ast_log = args.GetProperty("-ast_log");
	ASTConsumer ast_consumer(ast_context, db, reflection_specs, ast_log);
	ast_consumer.WalkTranlationUnit(ast_context.getTranslationUnitDecl());

	float build = clock();

	// Add all the container specs
	const ReflectionSpecContainer::MapType& container_specs = reflection_specs.GetContainerSpecs();
	for (ReflectionSpecContainer::MapType::const_iterator i = container_specs.begin(); i != container_specs.end(); ++i)
	{
		const ReflectionSpecContainer& c = i->second;
		db.AddContainerInfo(i->first, c.read_iterator_type, c.write_iterator_type, c.has_key);
	}

	// Write included header files if requested
	std::string output_headers = args.GetProperty("-output_headers");
	if (output_headers!="")
		WriteIncludedHeaders(parser, output_headers.c_str(), input_filename);

	// Write to a text/binary database depending upon extension
	std::string output = args.GetProperty("-output");
	if (output != "")
		WriteDatabase(db, output);

	float dbwrite = clock();

	if (args.Have("-test_db"))
		TestDBReadWrite(db);

	float end = clock();

	// Print some rough profiling info
	if (args.Have("-timing"))
	{
		printf("Prologue:   %.3f\n", (prologue - start) / CLOCKS_PER_SEC);
		printf("Parsing:    %.3f\n", (parsing - prologue) / CLOCKS_PER_SEC);
		printf("Specs:      %.3f\n", (specs - parsing) / CLOCKS_PER_SEC);
		printf("Building:   %.3f\n", (build - specs) / CLOCKS_PER_SEC);
		printf("Database:   %.3f\n", (dbwrite - build) / CLOCKS_PER_SEC);
		printf("Total time: %.3f\n", (end - start) / CLOCKS_PER_SEC);
	}

	return 0;
}
