
#pragma once


namespace crdb
{
	class Database;
}


class MapFileParser
{
public:
	MapFileParser(crdb::Database& db, const char* filename);
private:
};