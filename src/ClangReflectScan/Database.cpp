
#include "Database.h"
#include <stdlib.h>
#include <assert.h>


namespace
{
	crdb::u32 fmix(crdb::u32 h)
	{
		// Finalisation mix - force all bits of a hash block to avalanche
		h ^= h >> 16;
		h *= 0x85ebca6b;
		h ^= h >> 13;
		h *= 0xc2b2ae35;
		h ^= h >> 16;
		return h;
	}


	template <typename TYPE, typename DBTYPE>
	void AddPrimitive(crdb::Name name, crdb::Name noname, TYPE object, DBTYPE& db)
	{
		// Add to unnamed vector or named multimap
		if (name == noname)
		{
			db.unnamed.push_back(object);
		}

		else
		{
			db.named.insert(DBTYPE::NamedStore::value_type(name->first, object));
		}
	}
}


crdb::u32 crdb::MurmurHash3(const void* key, int len, u32 seed)
{
	const u8* data = (const u8*)key;
	int nb_blocks = len / 4;

	u32 h1 = seed;
	u32 c1 = 0xcc9e2d51;
	u32 c2 = 0x1b873593;

	// Body
	const u32* blocks = (const u32*)(data + nb_blocks * 4);
	for (int i = -nb_blocks; i; i++)
	{
		u32 k1 = blocks[i];

		k1 *= c1;
		k1 = _rotl(k1, 15);
		k1 *= c2;

		h1 ^= k1;
		h1 = _rotl(h1, 13);
		h1 = h1 * 5 + 0xe6546b64;
	}

	// Tail
	const u8* tail = (const u8*)(data + nb_blocks * 4);
	u32 k1 = 0;
	switch (len & 3)
	{
		case (3): k1 ^= tail[2] << 16;
		case (2): k1 ^= tail[1] << 8;
		case (1): k1 ^= tail[0];
				  k1 *= c1;
				  k1 = _rotl(k1, 15);
				  k1 *= c2;
				  h1 ^= k1;
	}

	// Finalisation
	h1 ^= len;
	h1 = fmix(h1);
	return h1;
}


crdb::Database::Database()
{
	// Create the global namespace that everything should ultimately reference
	Name parent = GetNoName();
	AddPrimitive(Namespace(parent, m_Names.end()));

	// Create a selection of basic C++ types
	AddPrimitive(Type(GetName("void"), parent));
	AddPrimitive(Type(GetName("bool"), parent));
	AddPrimitive(Type(GetName("char"), parent));
	AddPrimitive(Type(GetName("unsigned char"), parent));
	AddPrimitive(Type(GetName("short"), parent));
	AddPrimitive(Type(GetName("unsigned short"), parent));
	AddPrimitive(Type(GetName("int"), parent));
	AddPrimitive(Type(GetName("unsigned int"), parent));
	AddPrimitive(Type(GetName("long"), parent));
	AddPrimitive(Type(GetName("unsigned long"), parent));
	AddPrimitive(Type(GetName("float"), parent));
	AddPrimitive(Type(GetName("double"), parent));
}


crdb::Name crdb::Database::GetNoName()
{
	return m_Names.end();
}


crdb::Name crdb::Database::GetName(const char* text)
{
	if (text == 0)
	{
		return GetNoName();
	}

	// See if the name has already been created
	u32 hash = MurmurHash3(text, strlen(text), 0);
	Name name = m_Names.find(hash);
	if (name != m_Names.end())
	{
		// Check for collision
		assert(name->second == std::string(text) && "Hash collision!");
		return name;
	}

	// Add to the database
	return m_Names.insert(NameMap::value_type(hash, text)).first;
}


void crdb::Database::AddPrimitive(const Namespace& prim)
{
	::AddPrimitive(prim.name, m_Names.end(), prim, m_Namespaces);
}


void crdb::Database::AddPrimitive(const Type& prim)
{
	::AddPrimitive(prim.name, m_Names.end(), prim, m_Types);
}


void crdb::Database::AddPrimitive(const Class& prim)
{
	::AddPrimitive(prim.name, m_Names.end(), prim, m_Classes);
}


void crdb::Database::AddPrimitive(const Enum& prim)
{
	::AddPrimitive(prim.name, m_Names.end(), prim, m_Enums);
}


void crdb::Database::AddPrimitive(const EnumConstant& prim)
{
	::AddPrimitive(prim.name, m_Names.end(), prim, m_EnumConstants);
}


void crdb::Database::AddPrimitive(const Function& prim)
{
	::AddPrimitive(prim.name, m_Names.end(), prim, m_Functions);
}


void crdb::Database::AddPrimitive(const Parameter& prim)
{
	::AddPrimitive(prim.name, m_Names.end(), prim, m_Parameters);
}


void crdb::Database::AddPrimitive(const Field& prim)
{
	::AddPrimitive(prim.name, m_Names.end(), prim, m_Fields);
}
