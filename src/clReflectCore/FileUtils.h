
//
// ===============================================================================
// clReflect, FileUtils.h - Random collection of file/string utilities.
// -------------------------------------------------------------------------------
// Copyright (c) 2011 Don Williamson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ===============================================================================
//


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


bool startswith(const char* text, const char* cmp);
bool startswith(const std::string& text, const char* cmp);


const char* SkipWhitespace(const char* text);


const char* ConsumeToken(const char* text, char delimiter, char* dest, int dest_size);


std::string StringReplace(const std::string& str, const std::string& find, const std::string& replace);


