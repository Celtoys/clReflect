
#pragma once


namespace crcpp
{
	struct DatabaseFileHeader
	{
		// Initialises the file header to the current supported version
		DatabaseFileHeader();

		unsigned char signature[7];
		unsigned int version;

		int nb_ptr_schemas;
		int nb_ptr_relocations;

		unsigned int data_size;
	};
}