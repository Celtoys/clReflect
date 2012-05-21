
//
// ===============================================================================
// clReflect, FileUtils.h - Random collection of file/string utilities.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//


#pragma once


#include <clcpp/clcpp.h>


#include <cstdio>
#include <cstdlib>
#include <string>


char* ReadLine(FILE* fp);


// stores destination buffer locally
const char* itoa(unsigned int value);


// stores destination buffer locally and zero-prefixes
const char* itohex(unsigned int value);


clcpp::uint32 hextoi(const char* text);
clcpp::uint64 hextoi64(const char* text);


bool startswith(const char* text, const char* cmp);
bool startswith(const std::string& text, const char* cmp);


const char* SkipWhitespace(const char* text);


const char* ConsumeToken(const char* text, char delimiter, char* dest, int dest_size);


std::string StringReplace(const std::string& str, const std::string& find, const std::string& replace);


