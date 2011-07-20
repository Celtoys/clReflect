
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


	//
	// Austin Appleby's MurmurHash 3: http://code.google.com/p/smhasher
	//
	crdb::u32 MurmurHash3(const void* key, int len, crdb::u32 seed)
	{
		const crdb::u8* data = (const crdb::u8*)key;
		int nb_blocks = len / 4;

		crdb::u32 h1 = seed;
		crdb::u32 c1 = 0xcc9e2d51;
		crdb::u32 c2 = 0x1b873593;

		// Body
		const crdb::u32* blocks = (const crdb::u32*)(data + nb_blocks * 4);
		for (int i = -nb_blocks; i; i++)
		{
			crdb::u32 k1 = blocks[i];

			k1 *= c1;
			k1 = _rotl(k1, 15);
			k1 *= c2;

			h1 ^= k1;
			h1 = _rotl(h1, 13);
			h1 = h1 * 5 + 0xe6546b64;
		}

		// Tail
		const crdb::u8* tail = (const crdb::u8*)(data + nb_blocks * 4);
		crdb::u32 k1 = 0;
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
}


crdb::u32 crdb::HashNameString(const char* name_string)
{
	return MurmurHash3(name_string, strlen(name_string), 0);
}


crdb::u32 crdb::MixHashes(u32 a, u32 b)
{
	return MurmurHash3(&b, sizeof(u32), a);
}


crdb::Database::Database()
{
}


void crdb::Database::AddBaseTypePrimitives()
{
	// Create a selection of basic C++ types
	Name parent;
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


const crdb::Name& crdb::Database::GetName(const char* text)
{
	// Check for nullptr and empty string representations of a "noname"
	static Name noname;
	if (text == 0)
	{
		return noname;
	}
	u32 hash = HashNameString(text);
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
