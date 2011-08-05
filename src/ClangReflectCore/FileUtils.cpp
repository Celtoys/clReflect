
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
	return _itoa(value, text, 10);
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