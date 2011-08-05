
#include "MapFileParser.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>

#include <ClangReflectCore/FileUtils.h>
#include <ClangReflectCore/Logging.h>

#include <cstdio>
#include <cstring>


namespace
{
	bool InitialiseSymbolHandler()
	{
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
		if (!SymInitialize(GetCurrentProcess(), 0, TRUE))
		{
			LOG(main, ERROR, "Couldn't initialise symbol handler - no function addresses will be available!");
			return false;
		}

		return true;
	}


	void ShutdownSymbolHandler()
	{
		SymCleanup(GetCurrentProcess());
	}


	void ParseFunctionName(const char* function_name)
	{
	}
}


MapFileParser::MapFileParser(const char* filename)
{
	if (!InitialiseSymbolHandler())
	{
		return;
	}

	FILE* fp = fopen(filename, "rb");
	if (fp == 0)
	{
		return;
	}

	bool public_symbols = false;
	while (const char* line = ReadLine(fp))
	{
		if (public_symbols)
		{
			char token[1024];

			// Consume everything up to the function name
			line = SkipWhitespace(line);
			line = ConsumeToken(line, ' ', token, sizeof(token));
			line = SkipWhitespace(line);
			line = ConsumeToken(line, ' ', token, sizeof(token));

			// Undecorate the symbol name
			char function_name[1024];
			UnDecorateSymbolName(token, function_name, sizeof(function_name), UNDNAME_NAME_ONLY);
			char function_signature[1024];
			UnDecorateSymbolName(token, function_signature, sizeof(function_signature),
				UNDNAME_COMPLETE |
				UNDNAME_NO_ACCESS_SPECIFIERS |
				UNDNAME_NO_ALLOCATION_LANGUAGE |
				UNDNAME_NO_ALLOCATION_MODEL |
				UNDNAME_NO_MEMBER_TYPE |
				UNDNAME_NO_SPECIAL_SYMS |
				UNDNAME_NO_THROW_SIGNATURES);

			// Parse the address field
			line = SkipWhitespace(line);
			line = ConsumeToken(line, ' ', token, sizeof(token));
			unsigned int function_address = hextoi(token);

			line = SkipWhitespace(line);
			if (line[0] == 'f')
			{
				ParseFunctionName(function_name);
			}
		}

		// Look for the start of the public symbols descriptors
		if (strstr(line, "  Address"))
		{
			ReadLine(fp);
			public_symbols = true;
		}
	}

	fclose(fp);

	ShutdownSymbolHandler();
}