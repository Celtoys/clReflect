
#include "Database.h"
#include "Logging.h"

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


	void CheckClassMergeFailure(const crdb::Class& class_a, const crdb::Class& class_b)
	{
		const char* class_name = class_a.name->second.c_str();

		// This has to be the same class included multiple times in different translation units
		// Ensure that their descriptions match up as best as possible at this point
		if (class_a.base_class != class_b.base_class)
		{
			LOG(main, WARNING, "Class %s differs in base class specification during merge\n", class_name);
		}
		if (class_a.size != class_b.size)
		{
			LOG(main, WARNING, "Class %s differs in size during merge\n", class_name);
		}
	}


	template <typename TYPE>
	void MergeUniques(
		crdb::Database& db,
		const typename crdb::PrimitiveStore<TYPE>& src_store,
		typename crdb::PrimitiveStore<TYPE>& dest_store,
		void (*check_failure)(const TYPE&, const TYPE&) = 0)
	{
		// Add primitives that don't already exist for primitives where the symbol name can't be overloaded
		for (crdb::PrimitiveStore<TYPE>::const_iterator src = src_store.begin();
			src != src_store.end();
			++src)
		{
			crdb::PrimitiveStore<TYPE>::const_iterator dest = dest_store.find(src->first);
			if (dest == dest_store.end())
			{
				db.AddPrimitive(src->second);
			}

			else if (check_failure != 0)
			{
				check_failure(src->second, dest->second);
			}
		}
	}


	bool EnumConstantsAreEqual(const crdb::EnumConstant& enum_a, const crdb::EnumConstant& enum_b)
	{
		return enum_a.parent == enum_b.parent;
	}


	bool FunctionsAreEqual(const crdb::Function& function_a, const crdb::Function& function_b)
	{
		return function_a.unique_id == function_b.unique_id;
	}


	template <typename TYPE>
	void MergeOverloads(
		crdb::Database& db,
		const typename crdb::PrimitiveStore<TYPE>& src_store,
		typename crdb::PrimitiveStore<TYPE>& dest_store)
	{
		// Unconditionally add primitives that don't already exist
		for (crdb::PrimitiveStore<TYPE>::const_iterator src = src_store.begin();
			 src != src_store.end();
			 ++src)
		{
			crdb::PrimitiveStore<TYPE>::const_iterator dest = dest_store.find(src->first);
			if (dest == dest_store.end())
			{
				db.AddPrimitive(src->second);
			}

			else
			{
				// A primitive of the same name exists so double-check all existing entries for a matching primitives before adding
				bool add = true;
				crdb::PrimitiveStore<TYPE>::const_range dest_range = dest_store.equal_range(src->first);
				for (crdb::PrimitiveStore<TYPE>::const_iterator i = dest_range.first; i != dest_range.second; ++i)
				{
					if (i->second == src->second)
					{
						add = false;
						break;
					}
				}

				if (add)
				{
					db.AddPrimitive(src->second);
				}
			}
		}
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
	Name parent = GetNoName();
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


crdb::Name crdb::Database::GetNoName() const
{
	return m_Names.end();
}


crdb::Name crdb::Database::GetName(const char* text)
{
	// Check for nullptr and empty string representations of a "noname"
	if (text == 0)
	{
		return GetNoName();
	}
	u32 hash = HashNameString(text);
	if (hash == 0)
	{
		return GetNoName();
	}

	// See if the name has already been created
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


crdb::Name crdb::Database::GetName(u32 hash) const
{
	return m_Names.find(hash);
}


void crdb::Database::Merge(const Database& db)
{
	// The symbol names for these primitives can't be overloaded
	MergeUniques<Namespace>(*this, db.m_Namespaces, m_Namespaces);
	MergeUniques<Type>(*this, db.m_Types, m_Types);
	MergeUniques<Enum>(*this, db.m_Enums, m_Enums);

	// Class symbol names can't be overloaded but extra checks can be used to make sure
	// the same class isn't violating the One Definition Rule
	MergeUniques<Class>(*this, db.m_Classes, m_Classes, CheckClassMergeFailure);

	// Add enum constants as if they are overloadable
	// NOTE: Technically don't need to do this enum constants are scoped. However, I might change
	// that in future so this code will become useful.
	MergeOverloads<EnumConstant>(*this, db.m_EnumConstants, m_EnumConstants);

	// Functions can be overloaded so rely on their unique id to merge them
	MergeOverloads<Function>(*this, db.m_Functions, m_Functions);

	// Field names aren't scoped and hence overloadable. They are parented to unique functions so that will
	// be the key deciding factor in whether fields should be merged or not.
	MergeOverloads<Field>(*this, db.m_Fields, m_Fields);

	// TODO: unnamed
}