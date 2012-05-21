
//
// ===============================================================================
// clReflect, DatabaseTextSerialiser.h - Text serialisation of the offline
// Reflection database. Used mainly during development for debugging.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//


#pragma once


namespace cldb
{
	class Database;

	void WriteTextDatabase(const char* filename, const Database& db);
	bool ReadTextDatabase(const char* filename, Database& db);
	bool IsTextDatabase(const char* filename);
}