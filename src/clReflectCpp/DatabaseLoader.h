
#pragma once


namespace clcpp
{
	struct IFile;

	namespace internal
	{
		struct DatabaseMem;


		struct DatabaseFileHeader
		{
			// Initialises the file header to the current supported version
			DatabaseFileHeader();

			// Signature and version numbers for verifying header integrity
			unsigned int signature0;
			unsigned int signature1;
			unsigned int version;

			int nb_ptr_schemas;
			int nb_ptr_offsets;
			int nb_ptr_relocations;

			unsigned int data_size;

			// TODO: CRC verify?
		};


		DatabaseMem* LoadMemoryMappedDatabase(IFile* file);
	}
}