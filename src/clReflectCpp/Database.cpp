
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

#include <clcpp\Database.h>
#include "DatabaseLoader.h"


namespace
{
	unsigned int GetNameHash(clcpp::Name name)
	{
		return name.hash;
	}
	unsigned int GetPrimitiveHash(const clcpp::Primitive& primitive)
	{
		return primitive.name.hash;
	}
	unsigned int GetPrimitivePtrHash(const clcpp::Primitive* primitive)
	{
		return primitive->name.hash;
	}


	template <typename ARRAY_TYPE, typename COMPARE_L_TYPE, unsigned int (GET_HASH_FUNC)(COMPARE_L_TYPE)>
	int BinarySearch(const clcpp::CArray<ARRAY_TYPE>& entries, unsigned int compare_hash)
	{
		// TODO: Return multiple entries

		int first = 0;
		int last = entries.size() - 1;

		// Binary search
		while (first <= last)
		{
			// Identify the mid point
			int mid = (first + last) / 2;

			unsigned entry_hash = GET_HASH_FUNC(entries[mid]);
			if (compare_hash > entry_hash)
			{
				// Shift search to local upper half
				first = mid + 1;
			}
			else if (compare_hash < entry_hash)
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


const clcpp::Primitive* clcpp::internal::FindPrimitive(const CArray<const Primitive*>& primitives, unsigned int hash)
{
	int index = BinarySearch<const Primitive*, const Primitive*, GetPrimitivePtrHash>(primitives, hash);
	if (index == -1)
	{
		return 0;
	}
	return primitives[index];
}


clcpp::Database::Database()
	: m_DatabaseMem(0)
	, m_Allocator(0)
{
}


clcpp::Database::~Database()
{
	if (m_DatabaseMem)
	{
		m_Allocator->Free(m_DatabaseMem);
	}
}


bool clcpp::Database::Load(IFile* file, IAllocator* allocator)
{
	internal::Assert(m_DatabaseMem == 0 && "Database already loaded");
	m_Allocator = allocator;
	m_DatabaseMem = internal::LoadMemoryMappedDatabase(file, m_Allocator);
	return m_DatabaseMem != 0;
}


void clcpp::Database::RebaseFunctions(unsigned int base_address)
{
	// Move all function addresses from their current location to their new location
	internal::Assert(m_DatabaseMem != 0);
	for (int i = 0; i < m_DatabaseMem->functions.size(); i++)
	{
		clcpp::Function& f = m_DatabaseMem->functions[i];
		f.address = f.address - m_DatabaseMem->function_base_address + base_address;
	}
}


clcpp::Name clcpp::Database::GetName(unsigned int hash) const
{
	// Lookup the name by hash
	int index = BinarySearch<Name, Name, GetNameHash>(m_DatabaseMem->names, hash);
	if (index == -1)
		return clcpp::Name();
	return m_DatabaseMem->names[index];
}


clcpp::Name clcpp::Database::GetName(const char* text) const
{
	// Null pointer
	if (text == 0)
		return clcpp::Name();

	// Hash and exit on no value
	unsigned int hash = internal::HashNameString(text);
	if (hash == 0)
		return clcpp::Name();

	return GetName(hash);
}


const clcpp::Type* clcpp::Database::GetType(unsigned int hash) const
{
	return FindPrimitive(m_DatabaseMem->type_primitives, hash);
}


const clcpp::Namespace* clcpp::Database::GetNamespace(unsigned int hash) const
{
	int index = BinarySearch<Namespace, const Primitive&, GetPrimitiveHash>(m_DatabaseMem->namespaces, hash);
	if (index == -1)
	{
		return 0;
	}
	return &m_DatabaseMem->namespaces[index];
}


const clcpp::Function* clcpp::Database::GetFunction(unsigned int hash) const
{
	int index = BinarySearch<Function, const Primitive&, GetPrimitiveHash>(m_DatabaseMem->functions, hash);
	if (index == -1)
	{
		return 0;
	}
	return &m_DatabaseMem->functions[index];
}
