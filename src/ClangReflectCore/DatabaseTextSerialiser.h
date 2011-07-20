
#pragma once


namespace crdb
{
	class Database;

	void WriteTextDatabase(const char* filename, const Database& db);
	bool ReadTextDatabase(const char* filename, Database& db);
	bool IsTextDatabase(const char* filename);
}