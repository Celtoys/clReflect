
#include <crcpp\Database.h>
#include "DatabaseLoader.h"


namespace
{
	int CompareNames(crcpp::Name name, unsigned int hash)
	{
		return hash - name.hash;
	}


	int ComparePrimitives(const crcpp::Primitive* primitive, unsigned int hash)
	{
		return hash - primitive->name.hash;
	}


	template <typename TYPE, typename COMPARE_TYPE, int (COMPARE_FUNC)(TYPE, COMPARE_TYPE)>
	int BinarySearch(const crcpp::CArray<TYPE>& entries, COMPARE_TYPE compare_value)
	{
		// TODO: Return multiple entries

		int first = 0;
		int last = entries.size() - 1;

		// Binary search
		while (first < last)
		{
			// Identify the mid point
			int mid = (first + last) / 2;

			int cmp = COMPARE_FUNC(entries[mid], compare_value);

			// hash - primitive_hash > 0
			if (cmp > 0)
			{
				// Shift search to local upper half
				first = mid + 1;
			}
			// hash - primitive_hash < 0
			else if (cmp < 0)
			{
				// Shift search to local lower half
				last = mid - 1;
			}
			else
			{
				// Exact match found
				return mid;
			}
		}

		return -1;
	}
}


const crcpp::Primitive* crcpp::FindPrimitiveImpl(const CArray<const Primitive*>& primitives, unsigned int hash)
{
	int index = BinarySearch<const Primitive*, unsigned int, ComparePrimitives>(primitives, hash);
	if (index == -1)
	{
		return 0;
	}
	return primitives[index];
}


crcpp::Database::Database()
	: m_DatabaseMem(0)
{
}


crcpp::Database::~Database()
{
	delete (char*)m_DatabaseMem;
}


crcpp::Name crcpp::Database::GetName(const char* text) const
{
	// Null pointer
	if (text == 0)
	{
		return crcpp::Name();
	}

	// Hash and exit on no value
	unsigned int hash = HashNameString(text);
	if (hash == 0)
	{
		return crcpp::Name();
	}

	// Lookup the name by hash and see
	int index = BinarySearch<Name, unsigned int, CompareNames>(m_DatabaseMem->names, hash);
	if (index == -1)
	{
		return crcpp::Name();
	}

	return m_DatabaseMem->names[index];
}


const crcpp::Type* crcpp::Database::GetType(unsigned int hash) const
{
	return FindPrimitive(m_DatabaseMem->type_primitives, hash);
}


bool crcpp::Database::Load(IFile* file)
{
	m_DatabaseMem = LoadMemoryMappedDatabase(file);
	return m_DatabaseMem != 0;
}
