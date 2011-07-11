
#pragma once


namespace crdb
{
	class Database;

	void WriteTextDatabase(const char* filename, const Database& db);
}