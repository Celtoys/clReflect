
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

#include "MapFileParser.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>

#include <clReflectCore/Database.h>
#include <clReflectCore/FileUtils.h>
#include <clReflectCore/Logging.h>

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


	cldb::Field MatchParameter(cldb::Database& db, const char*& ptr, const char* end, bool& is_this_call)
	{
		// TODO: Not exactly proud of this parsing code - started thinking it would be a simple problem
		// but the unseen complexity forced it to grow oddly. It works, though. Needs a rewrite.

		cldb::Field parameter;

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
				parameter.qualifier.op = cldb::Qualifier::REFERENCE;
			}
			else if (token[0] == '*')
			{
				parameter.qualifier.op = cldb::Qualifier::POINTER;
			}

			// Check for const qualification
			else if (!strcmp(token, "const"))
			{
				parameter.qualifier.is_const = true;
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


	void AddFunctionAddress(cldb::Database& db, const std::string& function_name, const std::string& function_signature, unsigned int function_address)
	{
		if (function_address == 0)
			return;

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
		cldb::Field return_parameter = MatchParameter(db, ptr, ptr + func_pos, is_this_call);
		cldb::Field* return_parameter_ptr = 0;
		if (return_parameter.type.text != "void")
			return_parameter_ptr = &return_parameter;

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

		std::vector<cldb::Field> parameters;
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
			cldb::Field this_parameter;
			this_parameter.type = db.GetName(type_name);
			this_parameter.qualifier.op = cldb::Qualifier::POINTER;
			parameters.push_back(this_parameter);
		}

		// Parse the parameters
		ptr = function_signature.c_str() + l_pos + 1;
		const char* end = function_signature.c_str() + r_pos;
		while (ptr < end)
		{
			cldb::Field parameter = MatchParameter(db, ptr, end, is_this_call);
			if (parameter.type.text != "void")
				parameters.push_back(parameter);
		}

		// Calculate the ID of the matching function
		cldb::u32 unique_id = cldb::CalculateFunctionUniqueID(return_parameter_ptr, parameters);

		// Search through all functions of the same name
		cldb::u32 function_hash = clcpp::internal::HashNameString(function_name.c_str());
		cldb::DBMap<cldb::Function>::range functions = db.m_Functions.equal_range(function_hash);
		for (cldb::DBMap<cldb::Function>::iterator i = functions.first; i != functions.second; ++i)
		{
			// Assign the function address when the unique IDs match
			cldb::Function& function = i->second;
			if (function.unique_id == unique_id)
			{
				function.address = function_address;
				break;
			}
		}
	}


	size_t SkipTypePrefix(const std::string& text, size_t pos)
	{
		if (!strncmp(text.c_str() + pos, "struct ", sizeof("struct")))
			pos += sizeof("struct");
		if (!strncmp(text.c_str() + pos, "class ", sizeof("class")))
			pos += sizeof("class");
		if (!strncmp(text.c_str() + pos, "enum ", sizeof("enum")))
			pos += sizeof("enum");
		return pos;
	}


	void AddGetTypeAddress(cldb::Database& db, const std::string& function_name, unsigned int function_address, bool is_get_type)
	{
		if (function_address == 0)
			return;

		// Isolate the template parameter list
		size_t pos = function_name.find('<');
		if (pos == std::string::npos)
		{
			LOG(main, ERROR, "Couldn't locate opening angle bracket of the GetType function");
			return;
		}
		pos++;

		// Skip the prefix
		pos = SkipTypePrefix(function_name, pos);

		// Locate the end of the typename of the template parameter
		size_t end_pos = function_name.find('>', pos);
		if (end_pos == std::string::npos)
		{
			LOG(main, ERROR, "Couldn't locate closing angle bracket of the GetType function");
			return;
		}

		// Generate the name for the type
		std::string type_name_str = function_name.substr(pos, end_pos - pos);
		cldb::Name type_name = db.GetName(type_name_str.c_str());
		if (type_name.hash == 0)
		{
			LOG(main, ERROR, "GetType can't be used for unreflected '%s' type", type_name_str.c_str());
			return;
		}

		// Add to the database
		if (is_get_type)
			db.m_GetTypeFunctions[type_name.hash].get_type_address = function_address;
		else
			db.m_GetTypeFunctions[type_name.hash].get_typename_address = function_address;
	}


	void AddClassImplFunction(cldb::Database& db, const std::string& function_signature, unsigned int function_address, bool is_constructor)
	{
		if (function_address == 0)
			return;

		// Isolate the parameter list
		size_t pos = function_signature.find('(');
		if (pos == std::string::npos)
		{
			LOG(main, ERROR, "Couldn't locate opening bracket of class impl function");
			return;
		}
		pos++;

		// Skip the prefix
		pos = SkipTypePrefix(function_signature, pos);

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
		cldb::Name parameter_type_name = db.GetName(parameter_type_name_str.c_str());
		cldb::Name parameter_name = db.GetName("this");

		// Generate a name for the new function
		std::string function_name_str = parameter_type_name_str + "::";
		if (is_constructor)
			function_name_str += "ConstructObject";
		else
			function_name_str += "DestructObject";
		cldb::Name function_name = db.GetName(function_name_str.c_str());

		// Create the parameter
		cldb::Field parameter(
			parameter_name,
			function_name,
			parameter_type_name,
			cldb::Qualifier(cldb::Qualifier::POINTER, false),
			0);

		// Generate a unique ID that binds the function and parameter together
		std::vector<cldb::Field> parameters;
		parameters.push_back(parameter);
		cldb::u32 unique_id = cldb::CalculateFunctionUniqueID(0, parameters);

		// Create the function and bind the parameter to it
		cldb::Function function(
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


	void AddConstructFunction(cldb::Database& db, const std::string& function_signature, unsigned int function_address)
	{
		AddClassImplFunction(db, function_signature, function_address, true);
	}


	void AddDestructFunction(cldb::Database& db, const std::string& function_signature, unsigned int function_address)
	{
		AddClassImplFunction(db, function_signature, function_address, false);
	}
}


MapFileParser::MapFileParser(cldb::Database& db, const char* filename)
	: m_PreferredLoadAddress(0)
{
	static const char* construct_object = "clcpp::internal::ConstructObject";
	static const char* destruct_object = "clcpp::internal::DestructObject";
	static const char* get_typename = "clcpp::GetTypeNameHash<";
	static const char* get_type = "clcpp::GetType<";

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

			// Undecorate the symbol name alone and see if it's a known clcpp function
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
			else if (startswith(function_name, get_type))
			{
				unsigned int function_address = ParseAddressField(line, function_name.c_str());
				AddGetTypeAddress(db, function_name, function_address, true);
			}
			else if (startswith(function_name, get_typename))
			{
				unsigned int function_address = ParseAddressField(line, function_name.c_str());
				AddGetTypeAddress(db, function_name, function_address, false);
			}

			// Otherwise see if it's a function in the database
			else if (const cldb::Function* function = db.GetFirstPrimitive<cldb::Function>(function_name.c_str()))
			{
				std::string function_signature = UndecorateFunctionSignature(token);
				unsigned int function_address = ParseAddressField(line, function_name.c_str());
				AddFunctionAddress(db, function_name, function_signature, function_address);
			}
		}

		// Look for the start of the public symbols descriptors
		else if (strstr(line, "  Address"))
		{
			ReadLine(fp);
			public_symbols = true;
		}

		// Parse the preferred load address
		if (m_PreferredLoadAddress == 0 && strstr(line, "Preferred load address is "))
		{
			line += sizeof("Preferred load address is ");
			char token[32];
			ConsumeToken(line , '\r', token, sizeof(token));
			m_PreferredLoadAddress = hextoi(token);
		}
	}

	fclose(fp);

	ShutdownSymbolHandler();
}