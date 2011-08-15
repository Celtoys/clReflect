
//
// ===============================================================================
// clReflect, CppExport.h - Exporting from the offline Reflection Database format
// to the runtime C++ version, with some pretty-printing tools.
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

#pragma once


#include "StackAllocator.h"
#include <map>
#include <vector>
#include <clcpp/clcpp.h>


namespace cldb
{
	class Database;
}


struct CppExport
{
	CppExport()
		: allocator(5 * 1024 * 1024)	// 5MB should do for now
		, db(0)
	{
	}

	StackAllocator allocator;

	clcpp::internal::DatabaseMem* db;

	// Hash of names for easier debugging
	typedef std::map<unsigned int, const char*> NameMap;
	NameMap name_map;
};


bool BuildCppExport(const cldb::Database& db, CppExport& cppexp);
void SaveCppExport(CppExport& cppexport, const char* filename);
void WriteCppExportAsText(const CppExport& cppexp, const char* filename);
