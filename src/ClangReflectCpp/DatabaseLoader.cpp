
#include "DatabaseLoader.h"
#include <crcpp/Core.h>

#include <cstdio>
#include <cstring>


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


	template <typename TYPE> void Read(TYPE& dest, FILE* fp)
	{
		// Anything with no overload of Read is a straight POD read
		fread(&dest, sizeof(dest), 1, fp);
	}
	template <typename TYPE> TYPE Read(FILE* fp)
	{
		TYPE temp;
		Read(temp, fp);
		return temp;
	}
}


crcpp::DatabaseFileHeader::DatabaseFileHeader()
	: version(1)
{
	memcpy(signature, "crcppdb", sizeof(signature));
}


crcpp::DatabaseMem* crcpp::LoadMemoryMappedDatabase(const char* filename)
{
	// Can the file be opened?
	FILE* fp = fopen(filename, "rb");
	if (fp == 0)
	{
		return 0;
	}

	// Read the header and verify the version and signature
	DatabaseFileHeader file_header, cmp_header;
	Read(file_header, fp);
	if (file_header.version != cmp_header.version)
	{
		fclose(fp);
		return 0;
	}
	if (memcmp(file_header.signature, cmp_header.signature, sizeof(cmp_header.signature)))
	{
		fclose(fp);
		return 0;
	}

	// Read the memory mapped data
	char* base_data = new char[file_header.data_size];
	DatabaseMem* database_mem = (DatabaseMem*)base_data;
	fread(base_data, file_header.data_size, 1, fp);

	// Read the schema descriptions
	CArray<PtrSchema> schemas(file_header.nb_ptr_schemas);
	fread(schemas.data(), sizeof(PtrSchema), file_header.nb_ptr_schemas, fp);

	// Read the pointer offsets for all the schemas
	CArray<int> ptr_offsets(file_header.nb_ptr_offsets);
	fread(ptr_offsets.data(), sizeof(int), file_header.nb_ptr_offsets, fp);

	// Read the pointer relocation instructions
	CArray<PtrRelocation> relocations(file_header.nb_ptr_relocations);
	fread(relocations.data(), sizeof(PtrRelocation), file_header.nb_ptr_relocations, fp);

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

				// Patch only if non-null
				if (ptr != 0)
				{
					ptr += (unsigned int)base_data;
					// TODO: verify pointer is within range of the memory map
				}
			}
		}
	}

	fclose(fp);

	return database_mem;
}