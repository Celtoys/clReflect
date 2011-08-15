
#pragma once


#include <vector>


namespace cldb
{
	struct Attribute;
	class Database;
}


std::vector<cldb::Attribute*> ParseAttributes(cldb::Database& db, const char* text, const char* filename, int line);
