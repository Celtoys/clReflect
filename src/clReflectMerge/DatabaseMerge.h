
//
// ===============================================================================
// clReflect, DatabaseMerge.h - Merging of offline Reflection Databases.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//


namespace cldb
{
	class Database;
}


void MergeDatabases(cldb::Database& dest_db, const cldb::Database& src_db);
