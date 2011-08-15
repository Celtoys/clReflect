
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

#include "PtrRelocator.h"
#include <cassert>


namespace
{
	template <typename TYPE_A, typename TYPE_B>
	inline size_t distance(const TYPE_A* from, const TYPE_B* to)
	{
		return (size_t)((char*)to - (char*)from);
	}
}


PtrRelocator::PtrRelocator(const void* start, size_t data_size)
	: m_Start((char*)start)
	, m_DataSize(data_size)
{
}


PtrSchema& PtrRelocator::AddSchema(size_t stride, PtrSchema* base_schema)
{
	// Construct a schema with no initial pointer offsets
	m_Schemas.push_back(PtrSchema());
	PtrSchema& schema = m_Schemas.back();
	schema.base_schema = base_schema;
	schema.stride = stride;

	// Allocate a handle
	schema.handle = m_SchemaLookup.size();
	m_SchemaLookup.push_back(&schema);

	// Copy base schema pointer offsets if a base is specified
	if (base_schema)
	{
		schema.ptr_offsets.insert(schema.ptr_offsets.end(), base_schema->ptr_offsets.begin(), base_schema->ptr_offsets.end());
	}

	return schema;
}


void PtrRelocator::AddPointers(const PtrSchema& schema, const void* data, int nb_objects)
{
	// No need to add null pointers for patching
	if (data == 0)
	{
		return;
	}

	PtrRelocation relocation;
	relocation.schema_handle = schema.handle;
	relocation.offset = distance(m_Start, data);
	relocation.nb_objects = nb_objects;
	m_Relocations.push_back(relocation);
}


void PtrRelocator::MakeRelative()
{
	// Process each relocation instruction
	for (size_t i = 0; i < m_Relocations.size(); i++)
	{
		PtrRelocation& reloc = m_Relocations[i];
		PtrSchema& schema = *m_SchemaLookup[reloc.schema_handle];

		// Iterate over every object
		for (int j = 0; j < reloc.nb_objects; j++)
		{
			size_t object_offset = reloc.offset + j * schema.stride;

			// Get a reference to each pointer in the current object
			for (size_t k = 0; k < schema.ptr_offsets.size(); k++)
			{
				unsigned int ptr_offset = object_offset + schema.ptr_offsets[k];
				char*& ptr = (char*&)*(m_Start + ptr_offset);

				// Only relocate if it's non-null
				if (ptr != 0)
				{
					size_t d = distance(m_Start, ptr);
					assert(d < m_DataSize);
					ptr = (char*)d;
				}
			}
		}
	}
}