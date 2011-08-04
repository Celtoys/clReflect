
#pragma once


#include <vector>
#include <list>
#include <crcpp/Core.h>


//
// Schema that describes the location of pointers within a type
//
struct PtrSchema
{
	PtrSchema()
		: base_schema(0)
		, handle(0)
		, stride(0)
	{
	}

	// Add a pointer offset to the schema with an optional offset that's added on top
	template <typename MEMBER_TYPE, typename OBJECT_TYPE>
	PtrSchema& operator () (MEMBER_TYPE (OBJECT_TYPE::*ptr), size_t offset = 0)
	{
		size_t ptr_offset = offsetof(OBJECT_TYPE, *ptr);
		ptr_offsets.push_back(ptr_offset + offset);
		return *this;
	}

	// Add a pointer offset manually
	PtrSchema& operator () (size_t ptr_offset)
	{
		ptr_offsets.push_back(ptr_offset);
		return *this;
	}

	// This is stored and used by schema construction only
	PtrSchema* base_schema;

	// Serialisation handle
	int handle;

	// Generally the type size
	size_t stride;

	// Array of offsets within the type
	std::vector<size_t> ptr_offsets;
};


//
// A pointer relocation instruction
//
struct PtrRelocation
{
	// Serialised schema handle
	int schema_handle;

	// Offset to add each pointer offset in the schema
	size_t offset;

	// Number of objects to relocate, with object stride determined by the schema
	int nb_objects;
};


//
// Class for building pointer schemas, relocation instructions and applying transformations
// to the pointers.
//
class PtrRelocator
{
public:
	PtrRelocator(const void* start, size_t data_size);

	// Add a new schema which doesn't have any pointer offsets beyond those it inherits
	PtrSchema& AddSchema(size_t stride, PtrSchema* base_schema);

	// Add pointers for any number of objects given the schema handle
	void AddPointers(const PtrSchema& schema, const void* data, int nb_objects = 1);

	// Helper for auto-calculating the type stride
	template <typename TYPE>
	PtrSchema& AddSchema(PtrSchema* base_schema = 0)
	{
		return AddSchema(sizeof(TYPE), base_schema);
	}

	// Helper for adding pointers for an array of objects
	template <typename TYPE>
	void AddPointers(const PtrSchema& schema, const crcpp::CArray<TYPE>& array)
	{
		assert(sizeof(TYPE) == schema.stride);
		AddPointers(schema, array.data(), array.size());
	}

	// Make all pointers relative to the start memory address
	void MakeRelative();

	const std::vector<PtrSchema*>& GetSchemas() const { return m_SchemaLookup; }
	const std::vector<PtrRelocation>& GetRelocations() const { return m_Relocations; }

private:
	// This is the front of the allocated memory where all pointers will be made relative to
	char* m_Start;
	size_t m_DataSize;

	// Stored as a list so that I can return pointers after each addition
	std::list<PtrSchema> m_Schemas;
	std::vector<PtrSchema*> m_SchemaLookup;

	std::vector<PtrRelocation> m_Relocations;
};
