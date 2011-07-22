
#pragma once


#include <map>
#include <vector>
#include <crcpp/crcpp.h>


namespace crdb
{
	class Database;
}


struct CppExport
{
	// A single collection of all names for easy read/write
	std::map<unsigned int, int> name_hash_map;
	std::vector<char> name_data;

	// Sorted lists of all primitives
	crcpp::CArray<crcpp::Type> types;
	crcpp::CArray<crcpp::EnumConstant> enum_constants;
	crcpp::CArray<crcpp::Enum> enums;
	crcpp::CArray<crcpp::Field> fields;
	crcpp::CArray<crcpp::Field> unnamed_fields;
	crcpp::CArray<crcpp::Function> functions;
	crcpp::CArray<crcpp::Class> classes;
	crcpp::CArray<crcpp::Namespace> namespaces;
};


void BuildCppExport(const crdb::Database& db, CppExport& cppexp);
void WriteCppExportAsText(const CppExport& cppexp, const char* filename);
