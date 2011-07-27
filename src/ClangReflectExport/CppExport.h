
#pragma once


#include "StackAllocator.h"
#include <map>
#include <vector>
#include <crcpp/crcpp.h>


namespace crdb
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

	crcpp::DatabaseMem* db;

	// Hash of names for easier debugging
	typedef std::map<unsigned int, const char*> NameMap;
	NameMap name_map;
};


void BuildCppExport(const crdb::Database& db, CppExport& cppexp);
void WriteCppExportAsText(const CppExport& cppexp, const char* filename);
