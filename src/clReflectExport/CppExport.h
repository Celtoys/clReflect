
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


void BuildCppExport(const cldb::Database& db, CppExport& cppexp);
void SaveCppExport(CppExport& cppexport, const char* filename);
void WriteCppExportAsText(const CppExport& cppexp, const char* filename);
