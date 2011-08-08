
#pragma once


#include <cstdio>
#include <cstdlib>
#include <string>


char* ReadLine(FILE* fp);


// stores destination buffer locally
const char* itoa(unsigned int value);


// stores destination buffer locally and zero-prefixes
const char* itohex(unsigned int value);


unsigned int hextoi(const char* text);


const char* SkipWhitespace(const char* text);


const char* ConsumeToken(const char* text, char delimiter, char* dest, int dest_size);


std::string StringReplace(const std::string& str, const std::string& find, const std::string& replace);


