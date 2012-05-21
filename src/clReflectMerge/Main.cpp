
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include "DatabaseMerge.h"

#include <clReflectCore/Arguments.h>
#include <clReflectCore/Logging.h>
#include <clReflectCore/Database.h>
#include <clReflectCore/DatabaseTextSerialiser.h>
#include <clReflectCore/DatabaseBinarySerialiser.h>


#include <clcpp/clcpp.h>


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

	cldb::Database db;
	for (size_t i = 2; i < args.Count(); i++)
	{
		const char* filename = args[i].c_str();

		// Try to load the database
		cldb::Database loaded_db;
		if (!cldb::ReadBinaryDatabase(filename, loaded_db))
		{
			if (!cldb::ReadTextDatabase(filename, loaded_db))
			{
				LOG(main, ERROR, "Couldn't read '%s' as binary or text database - does it exist?", filename);
				return 1;
			}
		}

		// Merge into the main one
		MergeDatabases(db, loaded_db);
	}

	// Save the result
	const char* output_filename = args[1].c_str();
	cldb::WriteTextDatabase(output_filename, db);

	return 0;
}