
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include "DatabaseBinarySerialiser.h"
#include "Database.h"
#include "DatabaseMetadata.h"

#include <memory.h>
#include <stdio.h>
#include <vector>


namespace
{
	// 'cldb'
	const unsigned int FILE_HEADER = 0x62647263;
	const unsigned int FILE_VERSION = 1;


	// Map from hash to a text attribute, mainly for binary serialisation of a
	// single translation unit
	std::map<cldb::u32, std::string> g_TextAttributeMap;


	template <typename TYPE>
	void Write(FILE* fp, const TYPE& val)
	{
		fwrite(&val, sizeof(val), 1, fp);
	}


	void Write(FILE* fp, const std::string& str)
	{
		int len = str.length();
		Write(fp, len);
		fwrite(str.c_str(), len, 1, fp);
	}


    // TODO: This files contains lots of size variable using int type, check if we need to change this
	template <typename TYPE, int SIZE>
	void CopyInteger(const cldb::Database&, char* dest, const char* source, int)
	{
		// Ensure the assumed size is the same as the machine size
		int assert_size_is_correct[sizeof(TYPE) == SIZE];
		(void)assert_size_is_correct;

		// Quick assign copy
		*(TYPE*)dest = *(TYPE*)source;
	}


	void CopyMemory(const cldb::Database&, char* dest, const char* source, int size)
	{
		memcpy(dest, source, size);
	}


	void CopyNameToHash(const cldb::Database& db, char* dest, const char* source, int)
	{
		// Strip the hash from the name
		cldb::Name& name = *(cldb::Name*)source;
		*(cldb::u32*)dest = name.hash;
	}


	void CopyStringToHash(const cldb::Database& db, char* dest, const char* source, int)
	{
		// Calculate the hash from the string
		std::string& str = *(std::string*)source;
		*(cldb::u32*)dest = clcpp::internal::HashNameString(str.c_str());
	}


	template <void COPY_FUNC(const cldb::Database&, char*, const char*, int)>
	void CopyStridedData(const cldb::Database& db, char* dest, const char* source, int nb_entries, int dest_stride, int source_stride, int field_size)
	{
		// The compiler should be able to inline the call the COPY_FUNC for each entry
		for (int i = 0; i < nb_entries; i++)
		{
			COPY_FUNC(db, dest, source, field_size);
			dest += dest_stride;
			source += source_stride;
		}
	}


	void CopyBasicFields(const cldb::Database& db, char* dest, const char* source, int nb_entries, int dest_stride, int source_stride, int field_size)
	{
		// Use memcpy as a last resort - try at least to use some big machine-size types
		switch (field_size)
		{
		case (1): CopyStridedData< CopyInteger<bool, 1> >(db, dest, source, nb_entries, dest_stride, source_stride, field_size); break;
		case (2): CopyStridedData< CopyInteger<short, 2> >(db, dest, source, nb_entries, dest_stride, source_stride, field_size); break;
		case (4): CopyStridedData< CopyInteger<int, 4> >(db, dest, source, nb_entries, dest_stride, source_stride, field_size); break;
		default: CopyStridedData< CopyMemory >(db, dest, source, nb_entries, dest_stride, source_stride, field_size); break;
		}
	}


	template <typename TYPE>
	void PackTable(const cldb::Database& db, const std::vector<TYPE>& table, const cldb::meta::DatabaseType& type, char* output)
	{
		// Walk up through the inheritance hierarhcy
		for (const cldb::meta::DatabaseType* cur_type = &type; cur_type; cur_type = cur_type->base_type)
		{
			// Pack a field at a time
			for (size_t i = 0; i < cur_type->fields.size(); i++)
			{
				const cldb::meta::DatabaseField& field = cur_type->fields[i];

				for (int j = 0; j < field.count; j++)
				{
					// Start at the offset from the field within the first object
					char* dest = output + field.packed_offset + j * field.packed_size;
					const char* source = (char*)&table.front() + field.offset + j * field.size;

					// Perform strided copies depending on field type - pass information about the root type
					switch (field.type)
					{
					case (cldb::meta::FIELD_TYPE_BASIC): CopyBasicFields(db, dest, source, table.size(), type.packed_size, type.size, field.size); break;
					case (cldb::meta::FIELD_TYPE_NAME): CopyStridedData<CopyNameToHash>(db, dest, source, table.size(), type.packed_size, type.size, field.size); break;
					case (cldb::meta::FIELD_TYPE_STRING): CopyStridedData<CopyStringToHash>(db, dest, source, table.size(), type.packed_size, type.size, field.size); break;
					default: break;
					}
				}
			}
		}
	}


	template <typename MAP_TYPE, typename TYPE>
	void CopyMapToTable(const MAP_TYPE& store, std::vector<TYPE>& table)
	{
		// Make a local copy of all entries in the table
		int dest_index = 0;
		table.resize(store.size());
		for (typename MAP_TYPE::const_iterator i = store.begin(); i != store.end(); ++i)
			table[dest_index++] = i->second;
	}


	template <typename TYPE>
	void WriteTable(FILE* fp, const cldb::Database& db, const cldb::meta::DatabaseTypes& dbtypes)
	{
		// Generate a memory-contiguous table
		std::vector<TYPE> table;
		CopyMapToTable(db.GetDBMap<TYPE>(), table);

		// Record the table size
		int table_size = table.size();
		Write(fp, table_size);

		if (table_size)
		{
			// Allocate enough memory to store the table in packed binary format
			const cldb::meta::DatabaseType& type = dbtypes.GetType<TYPE>();
			int packed_size = table_size * type.packed_size;
			char* data = new char[packed_size];

			// Binary pack the table
			PackTable(db, table, type, data);

			// Write to file and cleanup
			fwrite(data, packed_size, 1, fp);
			delete [] data;
		}
	}


	void WriteNameTable(FILE* fp, const cldb::Database& db)
	{
		// Write the table header
		int nb_names = db.m_Names.size();
		Write(fp, nb_names);

		// Write each name
		for (cldb::NameMap::const_iterator i = db.m_Names.begin(); i != db.m_Names.end(); ++i)
		{
			Write(fp, i->second.hash);
			Write(fp, i->second.text);
		}
	}


	void WriteTextAttributeTable(FILE* fp, const cldb::Database& db)
	{
		// Write the table header
		int nb_text_attributes = db.m_TextAttributes.size();
		Write(fp, nb_text_attributes);

		// Populate the hash map
		g_TextAttributeMap.clear();
		for (cldb::DBMap<cldb::TextAttribute>::const_iterator i = db.m_TextAttributes.begin(); i != db.m_TextAttributes.end(); ++i)
		{
			const std::string& text = i->second.value;
			cldb::u32 hash = clcpp::internal::HashNameString(text.c_str());
			g_TextAttributeMap[hash] = text;
		}

		// Write the hash map
		for (std::map<cldb::u32, std::string>::const_iterator i = g_TextAttributeMap.begin(); i != g_TextAttributeMap.end(); ++i)
		{
			Write(fp, i->first);
			Write(fp, i->second);
		}
	}
}


void cldb::WriteBinaryDatabase(const char* filename, const Database& db)
{
	FILE* fp = fopen(filename, "wb");

	// Write the header
	Write(fp, FILE_HEADER);
	Write(fp, FILE_VERSION);

	// Write each table with explicit ordering
	cldb::meta::DatabaseTypes dbtypes;
	WriteNameTable(fp, db);
	WriteTextAttributeTable(fp, db);
	WriteTable<cldb::Type>(fp, db, dbtypes);
	WriteTable<cldb::EnumConstant>(fp, db, dbtypes);
	WriteTable<cldb::Enum>(fp, db, dbtypes);
	WriteTable<cldb::Field>(fp, db, dbtypes);
	WriteTable<cldb::Function>(fp, db, dbtypes);
	WriteTable<cldb::Class>(fp, db, dbtypes);
	WriteTable<cldb::Template>(fp, db, dbtypes);
	WriteTable<cldb::TemplateType>(fp, db, dbtypes);
	WriteTable<cldb::Namespace>(fp, db, dbtypes);

	// Write attribute tables with explicit ordering
	WriteTable<cldb::FlagAttribute>(fp, db, dbtypes);
	WriteTable<cldb::IntAttribute>(fp, db, dbtypes);
	WriteTable<cldb::FloatAttribute>(fp, db, dbtypes);
	WriteTable<cldb::PrimitiveAttribute>(fp, db, dbtypes);
	WriteTable<cldb::TextAttribute>(fp, db, dbtypes);

	WriteTable<cldb::ContainerInfo>(fp, db, dbtypes);

	WriteTable<cldb::TypeInheritance>(fp, db, dbtypes);

	fclose(fp);
}


namespace
{
	template <typename TYPE>
	TYPE Read(FILE* fp)
	{
		TYPE val;
		fread(&val, sizeof(val), 1, fp);
		return val;
	}


	template <> std::string Read<std::string>(FILE* fp)
	{
		char data[1024];

		// Clamp length to the available buffer size
		int len = Read<int>(fp);
		len = len > sizeof(data) - 1 ? sizeof(data) - 1 : len;

		fread(data, len, 1, fp);
		data[len] = 0;
		return data;
	}


	void CopyHashToName(const cldb::Database& db, char* dest, const char* source, int)
	{
		// Write the name as looked up by the hash
		cldb::u32 hash = *(cldb::u32*)source;
		*(cldb::Name*)dest = db.GetName(hash);
	}


	void CopyHashToString(const cldb::Database& db, char* dest, const char* source, int)
	{
		// Write the name as looked up by the hash
		cldb::u32 hash = *(cldb::u32*)source;
		*(std::string*)dest = g_TextAttributeMap[hash];
	}


	template <typename TYPE>
	void UnpackTable(const cldb::Database& db, std::vector<TYPE>& table, const cldb::meta::DatabaseType& type, const char* input)
	{
		// Walk up through the inheritance hierarhcy
		for (const cldb::meta::DatabaseType* cur_type = &type; cur_type; cur_type = cur_type->base_type)
		{
			// Unpack a field at a time
			for (size_t i = 0; i < cur_type->fields.size(); i++)
			{
				const cldb::meta::DatabaseField& field = cur_type->fields[i];

				for (int j = 0; j < field.count; j++)
				{
					// Start at the offset from the field within the first object
					char* dest = (char*)&table.front() + field.offset + j * field.size;
					const char* source = input + field.packed_offset + j * field.packed_size;

					// Perform strided copies depending on field type - pass information about the root type
					switch (field.type)
					{
					case (cldb::meta::FIELD_TYPE_BASIC): CopyBasicFields(db, dest, source, table.size(), type.size, type.packed_size, field.size); break;
					case (cldb::meta::FIELD_TYPE_NAME): CopyStridedData<CopyHashToName>(db, dest, source, table.size(), type.size, type.packed_size, field.size); break;
					case (cldb::meta::FIELD_TYPE_STRING): CopyStridedData<CopyHashToString>(db, dest, source, table.size(), type.size, type.packed_size, field.size); break;
					default: break;
					}
				}
			}
		}
	}


	template <typename TYPE>
	void ReadTable(FILE* fp, cldb::Database& db, const cldb::meta::DatabaseTypes& dbtypes)
	{
		// Create a big enough table dest
		int table_size = Read<int>(fp);
		std::vector<TYPE> table(table_size);

		if (table_size)
		{
			// Allocate enough memory to store the entire table in packed binary format and read it from the file
			const cldb::meta::DatabaseType& type = dbtypes.GetType<TYPE>();
			int packed_size = table_size * type.packed_size;
			char* data = new char[packed_size];
			fread(data, packed_size, 1, fp);

			// Unpack the binary table
			UnpackTable(db, table, type, data);
			delete [] data;

			// Add to the database
			for (size_t i = 0; i < table.size(); i++)
				db.Add(table[i].name, table[i]);
		}
	}


	void ReadNameTable(FILE* fp, cldb::Database& db)
	{
		// Read the table header
		int nb_names = Read<int>(fp);

		// Read and populate each name
		for (int i = 0; i < nb_names; i++)
		{
			cldb::u32 hash = Read<cldb::u32>(fp);
			std::string str = Read<std::string>(fp);
			db.m_Names[hash] = cldb::Name(hash, str);
		}
	}


	void ReadTextAttributeTable(FILE* fp, const cldb::Database& db)
	{
		// Read the table header
		int nb_text_attributes = Read<int>(fp);

		// Read and populate the hash map
		g_TextAttributeMap.clear();
		for (int i = 0; i < nb_text_attributes; i++)
		{
			cldb::u32 hash = Read<cldb::u32>(fp);
			std::string text = Read<std::string>(fp);
			g_TextAttributeMap[hash] = text;
		}
	}
}


bool cldb::ReadBinaryDatabase(const char* filename, Database& db)
{
	if (!IsBinaryDatabase(filename))
	{
		return false;
	}

	FILE* fp = fopen(filename, "rb");

	// Discard the header
	Read<unsigned int>(fp);
	Read<unsigned int>(fp);

	// Read each table with explicit ordering
	cldb::meta::DatabaseTypes dbtypes;
	ReadNameTable(fp, db);
	ReadTextAttributeTable(fp, db);
	ReadTable<cldb::Type>(fp, db, dbtypes);
	ReadTable<cldb::EnumConstant>(fp, db, dbtypes);
	ReadTable<cldb::Enum>(fp, db, dbtypes);
	ReadTable<cldb::Field>(fp, db, dbtypes);
	ReadTable<cldb::Function>(fp, db, dbtypes);
	ReadTable<cldb::Class>(fp, db, dbtypes);
	ReadTable<cldb::Template>(fp, db, dbtypes);
	ReadTable<cldb::TemplateType>(fp, db, dbtypes);
	ReadTable<cldb::Namespace>(fp, db, dbtypes);

	// Read attribute tables with explicit ordering
	ReadTable<cldb::FlagAttribute>(fp, db, dbtypes);
	ReadTable<cldb::IntAttribute>(fp, db, dbtypes);
	ReadTable<cldb::FloatAttribute>(fp, db, dbtypes);
	ReadTable<cldb::PrimitiveAttribute>(fp, db, dbtypes);
	ReadTable<cldb::TextAttribute>(fp, db, dbtypes);

	ReadTable<cldb::ContainerInfo>(fp, db, dbtypes);

	ReadTable<cldb::TypeInheritance>(fp, db, dbtypes);

	fclose(fp);

	return true;
}


bool cldb::IsBinaryDatabase(const char* filename)
{
	// Not a database if the file can't be found
	FILE* fp = fopen(filename, "rb");
	if (fp == 0)
		return false;

	// Read the header and check it
	unsigned int header = Read<unsigned int>(fp);
	unsigned int version = Read<unsigned int>(fp);
	bool is_binary_db = header == FILE_HEADER && version == FILE_VERSION;

	fclose(fp);
	return is_binary_db;
}
