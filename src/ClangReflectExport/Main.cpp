
#include "CppExport.h"
#include "MapFileParser.h"

#include <ClangReflectCore\Arguments.h>
#include <ClangReflectCore\Logging.h>
#include <ClangReflectCore\Database.h>
#include <ClangReflectCore\DatabaseTextSerialiser.h>
#include <ClangReflectCore\DatabaseBinarySerialiser.h>


int main(int argc, const char* argv[])
{
	LOG_TO_STDOUT(main, ALL);

	// Leave early if there aren't enough arguments
	Arguments args(argc, argv);
	if (args.Count() < 3)
	{
		LOG(main, ERROR, "Not enough arguments\n");
		return 1;
	}

	// Try to load the database
	const char* input_filename = args[1].c_str();
	crdb::Database db;
	if (!crdb::ReadBinaryDatabase(input_filename, db))
	{
		if (!crdb::ReadTextDatabase(input_filename, db))
		{
			LOG(main, ERROR, "Couldn't read '%s' as binary or text database - does it exist?", input_filename);
			return 1;
		}
	}

	std::string cpp_export = args.GetProperty("-cpp");
	if (cpp_export != "")
	{
		CppExport cppexp;
		BuildCppExport(db, cppexp);
		WriteCppExportAsText(cppexp, "out.txt");
		SaveCppExport(cppexp, cpp_export.c_str());
	}

	MapFileParser parser("../../out2.map");

	return 0;
}