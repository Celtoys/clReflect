
#pragma once


namespace cldb
{
	class Database;

	void WriteBinaryDatabase(const char* filename, const Database& db);
	bool ReadBinaryDatabase(const char* filename, Database& db);
	bool IsBinaryDatabase(const char* filename);
}