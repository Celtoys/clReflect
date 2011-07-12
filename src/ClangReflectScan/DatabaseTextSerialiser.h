
#pragma once


namespace crdb
{
	class Database;

	void WriteTextDatabase(const char* filename, const Database& db);

	void ReadTextDatabase(const char* filename, Database& db);
}