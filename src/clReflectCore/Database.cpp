
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

#include "Database.h"

#include <clcpp/Core.h>

#include <stdlib.h>
#include <assert.h>


namespace
{
	cldb::u32 CalcFieldHash(const cldb::Field& field)
	{
		// Construct the fully-qualified type name and hash that
		std::string name;
		name += field.is_const ? "const " : "";
		name += field.type.text;
		name += field.modifier == cldb::Field::MODIFIER_POINTER ? "*" : field.modifier == cldb::Field::MODIFIER_REFERENCE ? "&" : "";
		return clcpp::internal::HashNameString(name.c_str());
	}
}


cldb::u32 cldb::CalculateFunctionUniqueID(const Field* return_parameter, const std::vector<Field>& parameters)
{
	// The return parameter is optional as it may be void
	cldb::u32 unique_id = 0;
	if (return_parameter != 0)
	{
		unique_id = CalcFieldHash(*return_parameter);
	}

	// Mix with all parameter field hashes
	for (size_t i = 0; i < parameters.size(); i++)
	{
		cldb::u32 field_hash = CalcFieldHash(parameters[i]);
		unique_id = clcpp::internal::MixHashes(unique_id, field_hash);
	}

	return unique_id;
}


cldb::Database::Database()
{
}


void cldb::Database::AddBaseTypePrimitives()
{
	// Create a selection of basic C++ types
	// TODO: Figure the size of these out based on platform
	Name parent;
	AddPrimitive(Type(GetName("void"), parent, 0));
	AddPrimitive(Type(GetName("bool"), parent, sizeof(bool)));
	AddPrimitive(Type(GetName("char"), parent, sizeof(char)));
	AddPrimitive(Type(GetName("unsigned char"), parent, sizeof(unsigned char)));
	AddPrimitive(Type(GetName("wchar_t"), parent, sizeof(wchar_t)));
	AddPrimitive(Type(GetName("short"), parent, sizeof(short)));
	AddPrimitive(Type(GetName("unsigned short"), parent, sizeof(unsigned short)));
	AddPrimitive(Type(GetName("int"), parent, sizeof(int)));
	AddPrimitive(Type(GetName("unsigned int"), parent, sizeof(unsigned int)));
	AddPrimitive(Type(GetName("long"), parent, sizeof(long)));
	AddPrimitive(Type(GetName("unsigned long"), parent, sizeof(unsigned long)));
	AddPrimitive(Type(GetName("float"), parent, sizeof(float)));
	AddPrimitive(Type(GetName("double"), parent, sizeof(double)));

	// 64-bit types as clang sees them
	AddPrimitive(Type(GetName("long long"), parent, sizeof(__int64)));
	AddPrimitive(Type(GetName("unsigned long long"), parent, sizeof(unsigned __int64)));
}


const cldb::Name& cldb::Database::GetName(const char* text)
{
	// Check for nullptr and empty string representations of a "noname"
	static Name noname;
	if (text == 0)
	{
		return noname;
	}
	u32 hash = clcpp::internal::HashNameString(text);
	if (hash == 0)
	{
		return noname;
	}

	// See if the name has already been created
	NameMap::iterator i = m_Names.find(hash);
	if (i != m_Names.end())
	{
		// Check for collision
		assert(i->second.text == text && "Hash collision!");
		return i->second;
	}

	// Add to the database
	i = m_Names.insert(NameMap::value_type(hash, Name(hash, text))).first;
	return i->second;
}


const cldb::Name& cldb::Database::GetName(u32 hash) const
{
	// Check for DB existence first
	NameMap::const_iterator i = m_Names.find(hash);
	if (i == m_Names.end())
	{
		static Name noname;
		return noname;
	}
	return i->second;
}
