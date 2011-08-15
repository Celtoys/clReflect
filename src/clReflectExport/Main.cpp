
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
		// First build the C++ export representation
		CppExport cppexp;
		if (!BuildCppExport(db, cppexp))
		{
			return 1;
		}

		// Pretty-print the result to the specified output file
		std::string cpp_log = args.GetProperty("-cpp_log");
		if (cpp_log != "")
		{
			WriteCppExportAsText(cppexp, cpp_log.c_str());
		}

		// Save to disk
		// NOTE: After this point the CppExport object is useless (TODO: fix)
		SaveCppExport(cppexp, cpp_export.c_str());
	}

	return 0;
}