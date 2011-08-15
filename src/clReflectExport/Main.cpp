
#include "CppExport.h"
#include "MapFileParser.h"

#include <clReflectCore/Arguments.h>
#include <clReflectCore/Logging.h>
#include <clReflectCore/Database.h>
#include <clReflectCore/DatabaseTextSerialiser.h>
#include <clReflectCore/DatabaseBinarySerialiser.h>


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
	cldb::Database db;
	if (!cldb::ReadBinaryDatabase(input_filename, db))
	{
		if (!cldb::ReadTextDatabase(input_filename, db))
		{
			LOG(main, ERROR, "Couldn't read '%s' as binary or text database - does it exist?", input_filename);
			return 1;
		}
	}

	// Add function address information from any specified map files
	std::string map_file = args.GetProperty("-map");
	if (map_file != "")
	{
		LOG(main, INFO, "Parsing map file: %s", map_file.c_str());
		MapFileParser parser(db, map_file.c_str());
	}

	std::string cpp_export = args.GetProperty("-cpp");
	if (cpp_export != "")
	{
		CppExport cppexp;
		BuildCppExport(db, cppexp);
		WriteCppExportAsText(cppexp, "out.txt");
		SaveCppExport(cppexp, cpp_export.c_str());
	}

	return 0;
}