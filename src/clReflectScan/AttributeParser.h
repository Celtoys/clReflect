
//
// ===============================================================================
// clReflect, AttributeParser.h - A lexer and parser for attributes specified
// in the client C++ code.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once

#include <vector>

namespace cldb
{
    struct Attribute;
    class Database;
}

std::vector<cldb::Attribute*> ParseAttributes(cldb::Database& db, const char* text, const char* filename, int line);
