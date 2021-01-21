
//
// ===============================================================================
// clReflect, CppExport.h - Exporting from the offline Reflection Database format
// to the runtime C++ version, with some pretty-printing tools.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once

#include "StackAllocator.h"
#include <map>
#include <vector>

namespace cldb
{
    class Database;
}

struct CppExport
{
    CppExport(clcpp::pointer_type function_base_address)
        : allocator(5 * 1024 * 1024) // 5MB should do for now
        , function_base_address(function_base_address)
        , db(0)
    {
    }

    StackAllocator allocator;

    clcpp::pointer_type function_base_address;
    clcpp::internal::DatabaseMem* db;

    // Hash of names for easier debugging
    typedef std::map<unsigned int, const char*> NameMap;
    NameMap name_map;
};

bool BuildCppExport(const cldb::Database& db, CppExport& cppexp);
void SaveCppExport(CppExport& cppexport, const char* filename);
void WriteCppExportAsText(const CppExport& cppexp, const char* filename);
