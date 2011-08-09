
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
			UNDNAME_NO_ALLOCATION_MODEL |
			UNDNAME_NO_MEMBER_TYPE |
			UNDNAME_NO_SPECIAL_SYMS |
			UNDNAME_NO_THROW_SIGNATURES
			);
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


	const char* ConsumeParameterToken(const char* text, char* dest, int dest_size)
	{
		char* end = dest + dest_size;
		while (*text
			&& *text != ' '
			&& *text != ','
			&& *text != ')'
			&& dest != end)
		{
			*dest++ = *text++;
		}
		*dest = 0;
		return text;
	}


	crdb::Field MatchParameter(crdb::Database& db, const char*& ptr, const char* end, bool& is_this_call)
	{
		crdb::Field parameter;

		const char* skip_tokens[] =
		{
			// Argument passing specifications
			"__cdecl",
			"__stdcall",
			"__fastcall",
			// Type modifiers
			"struct",
			"class",
			"enum"
		};

		char type_name[1024] = { 0 };
		char token[1024] = { 0 };
		is_this_call = false;

		// Loop reading tokens irrespective of order. Note that this parsing strategy won't distinguish between
		// the type of const-qualifier. However, only one mode of qualification is currently supported so this
		// will suffice for now.
		bool parse = true;
		while (parse && ptr < end)
		{
			ptr = ConsumeParameterToken(ptr, token, sizeof(token));
			ptr = SkipWhitespace(ptr);

			// Check for modifiers
			if (token[0] == '&')
			{
				parameter.modifier = crdb::Field::MODIFIER_REFERENCE;
			}
			else if (token[0] == '*')
			{
				parameter.modifier = crdb::Field::MODIFIER_POINTER;
			}

			// Check for const qualification
			else if (!strcmp(token, "const"))
			{
				parameter.is_const = true;
			}

			// Mark this calls so that we can add the this parameter first
			else if (!strcmp(token, "__thiscall"))
			{
				is_this_call = true;
			}

			// Check for any type prefixes
			else if (!strcmp(token, "unsigned") || !strcmp(token, "signed"))
			{
				strcpy(type_name, token);
				strcat(type_name, " ");
			}

			else
			{
				// First check to see if this token is to be ignored
				bool skip = false;
				for (int i = 0; i < sizeof(skip_tokens) / sizeof(skip_tokens[0]); i++)
				{
					if (!strcmp(token, skip_tokens[i]))
					{
						skip = true;
					}
				}

				if (skip == false)
				{
					// What's remaining must be the type name
					strcat(type_name, token);
				}
			}

			if (*ptr == ',' || *ptr == ')')
			{
				ptr++;
				break;
			}
		}

		parameter.type = db.GetName(type_name);
		return parameter;
	}


	void AddFunctionAddress(crdb::Database& db, const std::string& function_name, const std::string& function_signature, unsigned int function_address)
	{
		if (function_address == 0)
		{
			return;
		}

		// Find where the return type ends
		size_t func_pos = function_signature.find(function_name);
		if (func_pos == std::string::npos)
		{
			LOG(main, ERROR, "Couldn't locate function name in signature for '%s'", function_name.c_str());
			return;
		}

		// Parse the return parameter and only remember it if it's non-void
		bool is_this_call = false;
		const char* ptr = function_signature.c_str();
		crdb::Field return_parameter = MatchParameter(db, ptr, ptr + func_pos, is_this_call);
		crdb::Field* return_parameter_ptr = 0;
		if (return_parameter.type.text != "void")
		{
			return_parameter_ptr = &return_parameter;
		}

		// Isolate the parameters in the signature
		size_t l_pos = function_signature.find('(', func_pos);
		if (l_pos == std::string::npos)
		{
			LOG(main, ERROR, "Couldn't locate left bracket in signature for '%s'", function_name.c_str());
			return;
		}
		size_t r_pos = function_signature.find(')', l_pos);
		if (r_pos == std::string::npos)
		{
			LOG(main, ERROR, "Couldn't locate right bracket in signature for '%s'", function_name.c_str());
			return;
		}

		std::vector<crdb::Field> parameters;
		if (is_this_call)
		{
			// Find the end of the type name
			size_t rsep = function_name.rfind("::");
			if (rsep == std::string::npos)
			{
				LOG(main, ERROR, "Function declaration says it's __thiscall but no type found in the name of '%s'", function_name.c_str());
				return;
			}

			// Construct the type name
			char type_name[1024];
			strncpy(type_name, function_name.c_str(), rsep);
			type_name[rsep] = 0;

			// Add the this parameter at the beginning
			crdb::Field this_parameter;
			this_parameter.type = db.GetName(type_name);
			this_parameter.modifier = crdb::Field::MODIFIER_POINTER;
			parameters.push_back(this_parameter);
		}

		// Parse the parameters
		ptr = function_signature.c_str() + l_pos + 1;
		const char* end = function_signature.c_str() + r_pos;
		while (ptr < end)
		{
			crdb::Field parameter = MatchParameter(db, ptr, end, is_this_call);
			if (parameter.type.text != "void")
			{
				parameters.push_back(parameter);
			}
		}

		// Calculate the ID of the matching function
		crdb::u32 unique_id = crdb::CalculateFunctionUniqueID(return_parameter_ptr, parameters);

		// Search through all functions of the same name
		crdb::u32 function_hash = crcpp::internal::HashNameString(function_name.c_str());
		crdb::PrimitiveStore<crdb::Function>::range functions = db.m_Functions.equal_range(function_hash);
		for (crdb::PrimitiveStore<crdb::Function>::iterator i = functions.first; i != functions.second; ++i)
		{
			// Assign the function address when the unique IDs match
			crdb::Function& function = i->second;
			if (function.unique_id == unique_id)
			{
				function.address = function_address;
				break;
			}
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