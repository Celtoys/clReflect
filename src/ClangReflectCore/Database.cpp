
#include "Database.h"

#include <crcpp/Core.h>

#include <stdlib.h>
#include <assert.h>


crdb::Database::Database()
{
}


void crdb::Database::AddBaseTypePrimitives()
{
	// Create a selection of basic C++ types
	// TODO: Figure the size of these out based on platform
	Name parent;
	AddPrimitive(Type(GetName("void"), parent, 0));
	AddPrimitive(Type(GetName("bool"), parent, sizeof(bool)));
	AddPrimitive(Type(GetName("char"), parent, sizeof(char)));
	AddPrimitive(Type(GetName("unsigned char"), parent, sizeof(unsigned char)));
	AddPrimitive(Type(GetName("short"), parent, sizeof(short)));
	AddPrimitive(Type(GetName("unsigned short"), parent, sizeof(unsigned short)));
	AddPrimitive(Type(GetName("int"), parent, sizeof(int)));
	AddPrimitive(Type(GetName("unsigned int"), parent, sizeof(unsigned int)));
	AddPrimitive(Type(GetName("long"), parent, sizeof(long)));
	AddPrimitive(Type(GetName("unsigned long"), parent, sizeof(unsigned long)));
	AddPrimitive(Type(GetName("float"), parent, sizeof(float)));
	AddPrimitive(Type(GetName("double"), parent, sizeof(double)));
}


const crdb::Name& crdb::Database::GetName(const char* text)
{
	// Check for nullptr and empty string representations of a "noname"
	static Name noname;
	if (text == 0)
	{
		return noname;
	}
	u32 hash = crcpp::internal::HashNameString(text);
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


const crdb::Name& crdb::Database::GetName(u32 hash) const
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
