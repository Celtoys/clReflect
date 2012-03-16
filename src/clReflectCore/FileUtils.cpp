
//
// ===============================================================================
// clReflect
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

#include "FileUtils.h"

#include <cstring>


char* ReadLine(FILE* fp)
{
	static char line[4096];

	// Loop reading characters until EOF or EOL
	int pos = 0;
	while (true)
	{
		int c = fgetc(fp);
		if (c == EOF)
		{
			return 0;
		}
		if (c == '\n')
		{
			break;
		}

		// Only add if the line is below the length of the buffer
		if (pos < sizeof(line) - 1)
		{
			line[pos++] = c;
		}
	}

	// Null terminate and return
	line[pos] = 0;
	return line;
}


const char* itoa(unsigned int value)
{
	static const int MAX_SZ = 20;
	static char text[MAX_SZ];
#ifdef _MSC_VER
	return _itoa(value, text, 10);
#else
    snprintf(text, 10, "%u", value);
    return text;
#endif  // _MSC_VER
}


const char* itohex(unsigned int value)
{
	static const int MAX_SZ = 9;
	static char text[MAX_SZ];

	// Null terminate and start at the end
	text[MAX_SZ - 1] = 0;
	char* tptr = text + MAX_SZ - 1;

	// Loop through the value with radix 16
	do 
	{
		int v = value & 15;
		*--tptr = "0123456789abcdef"[v];
		value /= 16;
	} while (value);

	// Zero-pad whats left
	while (tptr != text)
	{
		*--tptr = '0';
	}

	return tptr;
}


unsigned int hextoi(const char* text)
{
	// Sum each radix 16 element
	unsigned int val = 0;
	for (const char* tptr = text, *end = text + strlen(text); tptr != end; ++tptr)
	{
		val *= 16;
		int v = *tptr >= 'a' ? *tptr - 'a' + 10 : *tptr - '0';
		val += v;
	}

	return val;
}


bool startswith(const char* text, const char* cmp)
{
	return strstr(text, cmp) == text;
}


bool startswith(const std::string& text, const char* cmp)
{
	return startswith(text.c_str(), cmp);
}


const char* SkipWhitespace(const char* text)
{
	while (*text == ' ' || *text == '\t')
		text++;
	return text;
}


const char* ConsumeToken(const char* text, char delimiter, char* dest, int dest_size)
{
	char* end = dest + dest_size;
	while (*text && *text != delimiter && dest != end)
	{
		*dest++ = *text++;
	}
	*dest = 0;
	return text;
}


std::string StringReplace(const std::string& str, const std::string& find, const std::string& replace)
{
	std::string res = str;
	for (size_t i = res.find(find); i != res.npos; i = res.find(find, i))
	{
		res.replace(i, find.length(), replace);
		i += replace.length();
	}
	return res;
}


