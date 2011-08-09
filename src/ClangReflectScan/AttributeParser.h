
#pragma once


#include <vector>


namespace crdb
{
	struct Attribute;
	class Database;
}


std::vector<crdb::Attribute*> ParseAttributes(crdb::Database& db, const char* text);