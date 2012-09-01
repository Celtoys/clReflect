
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

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
			LOG(main, ERROR, "Couldn't read '%s' as binary or text database - does it exist?\n", input_filename);
			return 1;
		}
	}

	// Add function address information from any specified map files
	std::string map_file = args.GetProperty("-map");
	clcpp::pointer_type function_base_address = 0;

	if (map_file != "")
	{
		LOG(main, INFO, "Parsing map file: %s\n", map_file.c_str());
		MapFileParser parser(db, map_file.c_str());
		function_base_address = parser.m_PreferredLoadAddress;
	}

	std::string cpp_export = args.GetProperty("-cpp");
	if (cpp_export != "")
	{
		// First build the C++ export representation
		CppExport cppexp(function_base_address);
		if (!BuildCppExport(db, cppexp))
			return 1;

		// Pretty-print the result to the specified output file
		std::string cpp_log = args.GetProperty("-cpp_log");
		if (cpp_log != "")
			WriteCppExportAsText(cppexp, cpp_log.c_str());

		// Save to disk
		// NOTE: After this point the CppExport object is useless (TODO: fix)
		SaveCppExport(cppexp, cpp_export.c_str());
	}

	return 0;
}
