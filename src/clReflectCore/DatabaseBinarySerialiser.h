
//
// ===============================================================================
// clReflect, DatabaseBinarySerialiser.h - Binary serialisation of the offline
// Reflection Database. Much faster and more compact than the text representation.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//


#pragma once


namespace cldb
{
	class Database;

	void WriteBinaryDatabase(const char* filename, const Database& db);
	bool ReadBinaryDatabase(const char* filename, Database& db);
	bool IsBinaryDatabase(const char* filename);
}