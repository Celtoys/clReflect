
#include "DatabaseMerge.h"

#include "ClangReflectCore/Arguments.h"
#include "ClangReflectCore/Logging.h"
#include "ClangReflectCore/Database.h"
#include "ClangReflectCore/DatabaseTextSerialiser.h"
#include "ClangReflectCore/DatabaseBinarySerialiser.h"


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

	crdb::Database db;
	for (size_t i = 2; i < args.Count(); i++)
	{
		const char* filename = args[i].c_str();

		// Try to load the database
		crdb::Database loaded_db;
		if (!crdb::ReadBinaryDatabase(filename, loaded_db))
		{
			if (!crdb::ReadTextDatabase(filename, loaded_db))
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
	crdb::WriteTextDatabase(output_filename, db);

	return 0;
}