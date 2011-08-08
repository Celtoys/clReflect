
#include "MapFileParser.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>

#include <ClangReflectCore/Database.h>
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


	std::string UndecorateFunctionName(const char* token)
	{
		char function_name[1024];
		UnDecorateSymbolName(token, function_name, sizeof(function_name), UNDNAME_NAME_ONLY);
		return function_name;
	}


	std::string UndecorateFunctionSignature(const char* token)
	{
		char function_signature[1024];
		UnDecorateSymbolName(token, function_signature, sizeof(function_signature),
			UNDNAME_COMPLETE |
			UNDNAME_NO_ACCESS_SPECIFIERS |
			UNDNAME_NO_ALLOCATION_LANGUAGE |
			UNDNAME_NO_ALLOCATION_MODEL |
			UNDNAME_NO_MEMBER_TYPE |
			UNDNAME_NO_SPECIAL_SYMS |
			UNDNAME_NO_THROW_SIGNATURES);
		return function_signature;
	}


	unsigned int ParseAddressField(const char* line, const char* function_name)
	{
		// First parse the address as hex
		char token[1024];
		line = SkipWhitespace(line);
		line = ConsumeToken(line, ' ', token, sizeof(token));
		unsigned int function_address = hextoi(token);

		// Double-check that the map file knows this is a function
		line = SkipWhitespace(line);
		if (line[0] != 'f')
		{
			LOG(main, ERROR, "Function '%s' is not a function symbol in the map file", function_name);
			return 0;
		}

		return function_address;
	}


	void AddFunctionAddress(crdb::Database& db, const std::string& function_name, const std::string& function_signature, unsigned int function_address)
	{
		if (function_address == 0)
		{
			return;
		}
	}


	void AddClassImplFunction(crdb::Database& db, const std::string& function_signature, unsigned int function_address, bool is_constructor)
	{
		if (function_address == 0)
		{
			return;
		}

		// Isolate the parameter list
		size_t pos = function_signature.find('(');
		if (pos == std::string::npos)
		{
			LOG(main, ERROR, "Couldn't locate opening bracket of class impl function");
			return;
		}
		pos++;

		// Skip the prefix
		if (!strncmp(function_signature.c_str() + pos, "struct ", sizeof("struct")))
		{
			pos += sizeof("struct");
		}
		if (!strncmp(function_signature.c_str() + pos, "class ", sizeof("class")))
		{
			pos += sizeof("class");
		}

		// Locate the end of the typename of the first parameter by checking for its
		// pointer spec and accounting for whitespace
		size_t end_pos = function_signature.find('*', pos);
		if (end_pos == std::string::npos)
		{
			LOG(main, ERROR, "Couldn't locate pointer character for first parameter of class impl function");
			return;
		}
		while (function_signature[end_pos] == ' ' || function_signature[end_pos] == '*')
			end_pos--;

		// Generate the names for the parameter
		std::string parameter_type_name_str = function_signature.substr(pos, end_pos - pos + 1);
		crdb::Name parameter_type_name = db.GetName(parameter_type_name_str.c_str());
		crdb::Name parameter_name = db.GetName("this");

		// Generate a name for the new function
		std::string function_name_str = parameter_type_name_str + "::";
		if (is_constructor)
		{
			function_name_str += "ConstructObject";
		}
		else
		{
			function_name_str += "DestructObject";
		}
		crdb::Name function_name = db.GetName(function_name_str.c_str());

		// Create the parameter
		crdb::Field parameter(
			parameter_name,
			function_name,
			parameter_type_name,
			crdb::Field::MODIFIER_POINTER,
			false,
			0);

		// Generate a unique ID that binds the function and parameter together
		std::vector<crdb::Field> parameters;
		parameters.push_back(parameter);
		crdb::u32 unique_id = crdb::CalculateFunctionUniqueID(0, parameters);

		// Create the function and bind the parameter to it
		crdb::Function function(
			function_name,
			parameter_type_name,
			unique_id);
		parameter.parent_unique_id = unique_id;

		// Record the transient function address that will be exported
		function.address = function_address;

		// Add the new primitives to the database
		db.AddPrimitive(parameter);
		db.AddPrimitive(function);
	}


	void AddConstructFunction(crdb::Database& db, const std::string& function_signature, unsigned int function_address)
	{
		AddClassImplFunction(db, function_signature, function_address, true);
	}


	void AddDestructFunction(crdb::Database& db, const std::string& function_signature, unsigned int function_address)
	{
		AddClassImplFunction(db, function_signature, function_address, false);
	}
}


MapFileParser::MapFileParser(crdb::Database& db, const char* filename)
{
	static const char* construct_object = "crcpp::internal::ConstructObject";
	static const char* destruct_object = "crcpp::internal::DestructObject";

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

			// Undecorate the symbol name alone and see if it's a known crcpp function
			std::string function_name = UndecorateFunctionName(token);
			if (function_name == construct_object)
			{
				std::string function_signature = UndecorateFunctionSignature(token);
				unsigned int function_address = ParseAddressField(line, function_name.c_str());
				AddConstructFunction(db, function_signature, function_address);
			}
			else if (function_name == destruct_object)
			{
				std::string function_signature = UndecorateFunctionSignature(token);
				unsigned int function_address = ParseAddressField(line, function_name.c_str());
				AddDestructFunction(db, function_signature, function_address);
			}

			// Otherwise see if it's a function in the database
			else if (const crdb::Function* function = db.GetFirstPrimitive<crdb::Function>(function_name.c_str()))
			{
				std::string function_signature = UndecorateFunctionSignature(token);
				unsigned int function_address = ParseAddressField(line, function_name.c_str());
				AddFunctionAddress(db, function_name, function_signature, function_address);
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