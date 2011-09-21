
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

#include "DatabaseLoader.h"
#include <clcpp/Core.h>


namespace
{
	struct PtrSchema
	{
		int stride;
		int ptrs_offset;
		int nb_ptrs;
	};

	
	struct PtrRelocation
	{
		int schema_handle;
		int offset;
		int nb_objects;
	};
}


clcpp::internal::DatabaseFileHeader::DatabaseFileHeader()
	: signature0('pclc')
	, signature1('\0bdp')
	, version(2)
	, nb_ptr_schemas(0)
	, nb_ptr_offsets(0)
	, nb_ptr_relocations(0)
	, data_size(0)
{
}


clcpp::internal::DatabaseMem* clcpp::internal::LoadMemoryMappedDatabase(IFile* file, IAllocator* allocator)
{
	// Read the header and verify the version and signature
	DatabaseFileHeader file_header, cmp_header;
	if (!file->Read(file_header))
	{
		return 0;
	}
	if (file_header.version != cmp_header.version)
	{
		return 0;
	}
	if (file_header.signature0 != cmp_header.signature0 || file_header.signature1 != cmp_header.signature1)
	{
		return 0;
	}

	// Read the memory mapped data
	char* base_data = (char*)allocator->Alloc(file_header.data_size);
	DatabaseMem* database_mem = (DatabaseMem*)base_data;
	if (!file->Read(base_data, file_header.data_size))
	{
		return 0;
	}

	// Read the schema descriptions
	CArray<PtrSchema> schemas(file_header.nb_ptr_schemas, allocator);
	if (!file->Read(schemas))
	{
		return 0;
	}

	// Read the pointer offsets for all the schemas
	CArray<int> ptr_offsets(file_header.nb_ptr_offsets, allocator);
	if (!file->Read(ptr_offsets))
	{
		return 0;
	}

	// Read the pointer relocation instructions
	CArray<PtrRelocation> relocations(file_header.nb_ptr_relocations, allocator);
	if (!file->Read(relocations))
	{
		return 0;
	}

	// Iterate over every relocation instruction
	for (int i = 0; i < file_header.nb_ptr_relocations; i++)
	{
		PtrRelocation& reloc = relocations[i];
		PtrSchema& schema = schemas[reloc.schema_handle];

		// Take a weak C-array pointer to the schema's pointer offsets (for bounds checking)
		CArray<int> schema_ptr_offsets(&ptr_offsets[schema.ptrs_offset], schema.nb_ptrs);

		// Iterate over all objects in the instruction
		for (int j = 0; j < reloc.nb_objects; j++)
		{
			int object_offset = reloc.offset + j * schema.stride;

			// All pointers in the schema
			for (int k = 0; k < schema.nb_ptrs; k++)
			{
				unsigned int ptr_offset = object_offset + schema_ptr_offsets[k];
				unsigned int& ptr = (unsigned int&)*(base_data + ptr_offset);

				// Ensure the pointer relocation is within range of the memory map
				internal::Assert(ptr <= file_header.data_size);

				// Patch only if non-null - these shouldn't exist in the patch list but there's
				// no harm in putting an extra check here.
				if (ptr != 0)
				{
					ptr += (unsigned int)base_data;
				}
			}
		}
	}

	return database_mem;
}