
#include "DatabaseLoader.h"
#include <crcpp/Core.h>


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


crcpp::internal::DatabaseFileHeader::DatabaseFileHeader()
	: signature0('pcrc')
	, signature1('bdp')
	, version(1)
	, nb_ptr_schemas(0)
	, nb_ptr_offsets(0)
	, nb_ptr_relocations(0)
	, data_size(0)
{
}


crcpp::internal::DatabaseMem* crcpp::internal::LoadMemoryMappedDatabase(IFile* file)
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
	char* base_data = new char[file_header.data_size];
	DatabaseMem* database_mem = (DatabaseMem*)base_data;
	if (!file->Read(base_data, file_header.data_size))
	{
		return 0;
	}

	// Read the schema descriptions
	CArray<PtrSchema> schemas(file_header.nb_ptr_schemas);
	if (!file->Read(schemas))
	{
		return 0;
	}

	// Read the pointer offsets for all the schemas
	CArray<int> ptr_offsets(file_header.nb_ptr_offsets);
	if (!file->Read(ptr_offsets))
	{
		return 0;
	}

	// Read the pointer relocation instructions
	CArray<PtrRelocation> relocations(file_header.nb_ptr_relocations);
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
				internal::Assert(ptr < file_header.data_size);

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