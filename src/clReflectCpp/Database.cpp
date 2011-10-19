
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


	template <typename ARRAY_TYPE, typename COMPARE_L_TYPE, unsigned int (GET_HASH_FUNC)(COMPARE_L_TYPE)>
	clcpp::Range SearchNeighbours(const clcpp::CArray<ARRAY_TYPE>& entries, unsigned int compare_hash, int index)
	{
		clcpp::Range range;
		range.first = index;
		range.last = index + 1;

		// Search either side of the result, gathering further matches
		while (range.first > 0 && GET_HASH_FUNC(entries[range.first - 1]) == compare_hash)
			range.first--;
		while (range.last < entries.size() && GET_HASH_FUNC(entries[range.last]) == compare_hash)
			range.last++;

		return range;
	}



	void RebaseFunctions(clcpp::internal::DatabaseMem& dbmem, unsigned int base_address)
	{
		// Move all function addresses from their current location to their new location
		for (int i = 0; i < dbmem.functions.size(); i++)
		{
			clcpp::Function& f = dbmem.functions[i];
			f.address = f.address - dbmem.function_base_address + base_address;
		}

		// Do the same for the GetType family of functions
		for (int i = 0; i < dbmem.get_type_functions.size(); i++)
		{
			clcpp::internal::GetTypeFunctions& f = dbmem.get_type_functions[i];
			f.get_typename_address = f.get_typename_address - dbmem.function_base_address + base_address;
			f.get_type_address = f.get_type_address - dbmem.function_base_address + base_address;
		}
	}


	void PatchGetTypeAddresses(clcpp::Database& db, clcpp::internal::DatabaseMem& dbmem)
	{
		for (int i = 0; i < dbmem.get_type_functions.size(); i++)
		{
			clcpp::internal::GetTypeFunctions& f = dbmem.get_type_functions[i];

			// Patch up the type name hash static variable
			if (f.get_typename_address)
			{
				unsigned char* mov_instruction = (unsigned char*)f.get_typename_address;
				clcpp::internal::Assert(*mov_instruction == 0xA1);
				unsigned int* hash_address = *(unsigned int**)(mov_instruction + 1);
				*hash_address = db.GetName(f.type_hash).hash;
			}

			// Patch up the type pointer static variable
			if (f.get_type_address)
			{
				unsigned char* mov_instruction = (unsigned char*)f.get_type_address;
				clcpp::internal::Assert(*mov_instruction == 0xA1);
				const clcpp::Type** type_address = *(const clcpp::Type***)(mov_instruction + 1);
				*type_address = db.GetType(f.type_hash);
			}
		}
	}


	template <typename TYPE>
	void ParentPrimitivesToDatabase(clcpp::CArray<TYPE>& primitives, clcpp::Database* database)
	{
		for (int i = 0; i < primitives.size(); i++)
			primitives[i].database = database;
	}
}


const clcpp::Primitive* clcpp::internal::FindPrimitive(const CArray<const Primitive*>& primitives, unsigned int hash)
{
	int index = BinarySearch<const Primitive*, const Primitive*, GetPrimitivePtrHash>(primitives, hash);
	if (index == -1)
		return 0;
	return primitives[index];
}


clcpp::Range clcpp::internal::FindOverloadedPrimitive(const CArray<const Primitive*>& primitives, unsigned int hash)
{
	// Search for the first entry
	int index = BinarySearch<const Primitive*, const Primitive*, GetPrimitivePtrHash>(primitives, hash);
	if (index == -1)
		return Range();

	// Look at its neighbours to widen the primitives found
	return SearchNeighbours<const Primitive*, const Primitive*, GetPrimitivePtrHash>(primitives, hash, index);
}


clcpp::Database::Database()
	: m_DatabaseMem(0)
	, m_Allocator(0)
{
}


clcpp::Database::~Database()
{
	if (m_DatabaseMem)
		m_Allocator->Free(m_DatabaseMem);
}


bool clcpp::Database::Load(IFile* file, IAllocator* allocator, unsigned int base_address, unsigned int options)
{
	// Load the database
	internal::Assert(m_DatabaseMem == 0 && "Database already loaded");
	m_Allocator = allocator;
	m_DatabaseMem = internal::LoadMemoryMappedDatabase(file, m_Allocator);

	if (m_DatabaseMem != 0)
	{
		// If no base address is provided, rebasing will not occur and it is assumed the addresses
		// loaded are already correct. Rebasing is usually only needed for DLLs that have been moved
		// by the OS due to another DLL occupying its preferred load address.
		if (base_address != 0)
			RebaseFunctions(*m_DatabaseMem, base_address);

		if ((options & OPT_DONT_PATCH_GETTYPE) == 0)
			PatchGetTypeAddresses(*this, *m_DatabaseMem);

		// Tell each loaded primitive that they belong to this database
		ParentPrimitivesToDatabase(m_DatabaseMem->types, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->enum_constants, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->enums, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->fields, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->functions, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->classes, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->templates, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->template_types, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->namespaces, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->flag_attributes, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->int_attributes, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->float_attributes, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->name_attributes, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->text_attributes, this);
	}

	return m_DatabaseMem != 0;
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
		return 0;
	return &m_DatabaseMem->namespaces[index];
}


const clcpp::Function* clcpp::Database::GetFunction(unsigned int hash) const
{
	int index = BinarySearch<Function, const Primitive&, GetPrimitiveHash>(m_DatabaseMem->functions, hash);
	if (index == -1)
		return 0;
	return &m_DatabaseMem->functions[index];
}


clcpp::Range clcpp::Database::GetOverloadedFunction(unsigned int hash) const
{
	// Quickly locate the first match
	int index = BinarySearch<Function, const Primitive&, GetPrimitiveHash>(m_DatabaseMem->functions, hash);
	if (index == -1)
		return Range();

	// Functions can be overloaded so look at the neighbours to widen the primitives found
	return SearchNeighbours<Function, const Primitive&, GetPrimitiveHash>(m_DatabaseMem->functions, hash, index);
}