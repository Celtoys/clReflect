
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011 Don Williamson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
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