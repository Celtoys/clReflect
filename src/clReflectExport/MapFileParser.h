
//
// ===============================================================================
// clReflect, MapFileParser.h - Parsing of MAP files output from a link stage
// and storing of the addresses in the offline Reflection Database for reflected
// functions.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once


#include <clcpp/clcpp.h>


namespace cldb
{
	class Database;
}


struct MapFileParser
{
public:
	MapFileParser(cldb::Database& db, const char* filename);
	clcpp::pointer_type m_PreferredLoadAddress;
};
