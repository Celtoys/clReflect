
#pragma once


namespace crdb
{
	class Database;

	void WriteBinaryDatabase(const char* filename, const Database& db);

	void ReadBinaryDatabase(const char* filename, Database& db);
}