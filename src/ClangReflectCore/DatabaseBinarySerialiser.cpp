
#include "DatabaseBinarySerialiser.h"
#include "Database.h"
#include "DatabaseMetadata.h"

#include <vector>


namespace
{
	// 'crdb'
	const unsigned int FILE_HEADER = 0x62647263;
	const unsigned int FILE_VERSION = 1;


	// Map from hash to a text attribute, mainly for binary serialisation of a
	// single translation unit
	std::map<crdb::u32, std::string> g_TextAttributeMap;


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


	template <typename TYPE, int SIZE>
	void CopyInteger(const crdb::Database&, char* dest, const char* source, int)
	{
		// Ensure the assumed size is the same as the machine size
		int assert_size_is_correct[sizeof(TYPE) == SIZE];
		(void)assert_size_is_correct;

		// Quick assign copy
		*(TYPE*)dest = *(TYPE*)source;
	}


	void CopyMemory(const crdb::Database&, char* dest, const char* source, int size)
	{
		memcpy(dest, source, size);
	}


	void CopyNameToHash(const crdb::Database& db, char* dest, const char* source, int)
	{
		// Strip the hash from the name
		crdb::Name& name = *(crdb::Name*)source;
		*(crdb::u32*)dest = name.hash;
	}


	void CopyStringToHash(const crdb::Database& db, char* dest, const char* source, int)
	{
		// Calculate the hash from the string
		std::string& str = *(std::string*)source;
		*(crdb::u32*)dest = crcpp::internal::HashNameString(str.c_str());
	}


	template <void COPY_FUNC(const crdb::Database&, char*, const char*, int)>
	void CopyStridedData(const crdb::Database& db, char* dest, const char* source, int nb_entries, int dest_stride, int source_stride, int field_size)
	{
		// The compiler should be able to inline the call the COPY_FUNC for each entry
		for (int i = 0; i < nb_entries; i++)
		{
			COPY_FUNC(db, dest, source, field_size);
			dest += dest_stride;
			source += source_stride;
		}
	}


	void CopyBasicFields(const crdb::Database& db, char* dest, const char* source, int nb_entries, int dest_stride, int source_stride, int field_size)
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
	void PackTable(const crdb::Database& db, const std::vector<TYPE>& table, const crdb::meta::DatabaseType& type, char* output)
	{
		// Walk up through the inheritance hierarhcy
		for (const crdb::meta::DatabaseType* cur_type = &type; cur_type; cur_type = cur_type->base_type)
		{
			// Pack a field at a time
			for (size_t i = 0; i < cur_type->fields.size(); i++)
			{
				const crdb::meta::DatabaseField& field = cur_type->fields[i];

				for (int j = 0; j < field.count; j++)
				{
					// Start at the offset from the field within the first object
					char* dest = output + field.packed_offset + j * field.packed_size;
					const char* source = (char*)&table.front() + field.offset + j * field.size;

					// Perform strided copies depending on field type - pass information about the root type
					switch (field.type)
					{
					case (crdb::meta::FIELD_TYPE_BASIC): CopyBasicFields(db, dest, source, table.size(), type.packed_size, type.size, field.size); break;
					case (crdb::meta::FIELD_TYPE_NAME): CopyStridedData<CopyNameToHash>(db, dest, source, table.size(), type.packed_size, type.size, field.size); break;
					case (crdb::meta::FIELD_TYPE_STRING): CopyStridedData<CopyStringToHash>(db, dest, source, table.size(), type.packed_size, type.size, field.size); break;
					}
				}
			}
		}
	}


	template <typename TYPE>
	void CopyPrimitiveStoreToTable(const crdb::PrimitiveStore<TYPE>& store, std::vector<TYPE>& table)
	{
		int dest_index = 0;

		// Make a local copy of all entries in the table
		table.resize(store.size());
		for (crdb::PrimitiveStore<TYPE>::const_iterator i = store.begin(); i != store.end(); ++i)
		{
			table[dest_index++] = i->second;
		}
	}


	template <typename TYPE>
	void WriteTable(FILE* fp, const crdb::Database& db, const crdb::meta::DatabaseTypes& dbtypes)
	{
		// Generate a memory-contiguous table
		std::vector<TYPE> table;
		CopyPrimitiveStoreToTable(db.GetPrimitiveStore<TYPE>(), table);

		// Record the table size
		int table_size = table.size();
		Write(fp, table_size);

		if (table_size)
		{
			// Allocate enough memory to store the table in packed binary format
			const crdb::meta::DatabaseType& type = dbtypes.GetType<TYPE>();
			int packed_size = table_size * type.packed_size;
			char* data = new char[packed_size];

			// Binary pack the table
			PackTable(db, table, type, data);

			// Write to file and cleanup
			fwrite(data, packed_size, 1, fp);
			delete [] data;
		}
	}


	void WriteNameTable(FILE* fp, const crdb::Database& db)
	{
		// Write the table header
		int nb_names = db.m_Names.size();
		Write(fp, nb_names);

		// Write each name
		for (crdb::NameMap::const_iterator i = db.m_Names.begin(); i != db.m_Names.end(); ++i)
		{
			Write(fp, i->second.hash);
			Write(fp, i->second.text);
		}
	}


	void WriteTextAttributeTable(FILE* fp, const crdb::Database& db)
	{
		// Write the table header
		int nb_text_attributes = db.m_TextAttributes.size();
		Write(fp, nb_text_attributes);

		// Populate the hash map
		g_TextAttributeMap.clear();
		for (crdb::PrimitiveStore<crdb::TextAttribute>::const_iterator i = db.m_TextAttributes.begin(); i != db.m_TextAttributes.end(); ++i)
		{
			const std::string& text = i->second.value;
			crdb::u32 hash = crcpp::internal::HashNameString(text.c_str());
			g_TextAttributeMap[hash] = text;
		}

		// Write the hash map
		for (std::map<crdb::u32, std::string>::const_iterator i = g_TextAttributeMap.begin(); i != g_TextAttributeMap.end(); ++i)
		{
			Write(fp, i->first);
			Write(fp, i->second);
		}
	}
}


void crdb::WriteBinaryDatabase(const char* filename, const Database& db)
{
	FILE* fp = fopen(filename, "wb");

	// Write the header
	Write(fp, FILE_HEADER);
	Write(fp, FILE_VERSION);

	// Write each table with explicit ordering
	crdb::meta::DatabaseTypes dbtypes;
	WriteNameTable(fp, db);
	WriteTextAttributeTable(fp, db);
	WriteTable<crdb::Type>(fp, db, dbtypes);
	WriteTable<crdb::EnumConstant>(fp, db, dbtypes);
	WriteTable<crdb::Enum>(fp, db, dbtypes);
	WriteTable<crdb::Field>(fp, db, dbtypes);
	WriteTable<crdb::Function>(fp, db, dbtypes);
	WriteTable<crdb::Class>(fp, db, dbtypes);
	WriteTable<crdb::Template>(fp, db, dbtypes);
	WriteTable<crdb::TemplateType>(fp, db, dbtypes);
	WriteTable<crdb::Namespace>(fp, db, dbtypes);

	// Write attribute tables with explicit ordering
	WriteTable<crdb::FlagAttribute>(fp, db, dbtypes);
	WriteTable<crdb::IntAttribute>(fp, db, dbtypes);
	WriteTable<crdb::FloatAttribute>(fp, db, dbtypes);
	WriteTable<crdb::NameAttribute>(fp, db, dbtypes);
	WriteTable<crdb::TextAttribute>(fp, db, dbtypes);

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


	void CopyHashToName(const crdb::Database& db, char* dest, const char* source, int)
	{
		// Write the name as looked up by the hash
		crdb::u32 hash = *(crdb::u32*)source;
		*(crdb::Name*)dest = db.GetName(hash);
	}


	void CopyHashToString(const crdb::Database& db, char* dest, const char* source, int)
	{
		// Write the name as looked up by the hash
		crdb::u32 hash = *(crdb::u32*)source;
		*(std::string*)dest = g_TextAttributeMap[hash];
	}


	template <typename TYPE>
	void UnpackTable(const crdb::Database& db, std::vector<TYPE>& table, const crdb::meta::DatabaseType& type, const char* input)
	{
		// Walk up through the inheritance hierarhcy
		for (const crdb::meta::DatabaseType* cur_type = &type; cur_type; cur_type = cur_type->base_type)
		{
			// Unpack a field at a time
			for (size_t i = 0; i < cur_type->fields.size(); i++)
			{
				const crdb::meta::DatabaseField& field = cur_type->fields[i];

				for (int j = 0; j < field.count; j++)
				{
					// Start at the offset from the field within the first object
					char* dest = (char*)&table.front() + field.offset + j * field.size;
					const char* source = input + field.packed_offset + j * field.packed_size;

					// Perform strided copies depending on field type - pass information about the root type
					switch (field.type)
					{
					case (crdb::meta::FIELD_TYPE_BASIC): CopyBasicFields(db, dest, source, table.size(), type.size, type.packed_size, field.size); break;
					case (crdb::meta::FIELD_TYPE_NAME): CopyStridedData<CopyHashToName>(db, dest, source, table.size(), type.size, type.packed_size, field.size); break;
					case (crdb::meta::FIELD_TYPE_STRING): CopyStridedData<CopyHashToString>(db, dest, source, table.size(), type.size, type.packed_size, field.size); break;
					}
				}
			}
		}
	}


	template <typename TYPE>
	void ReadTable(FILE* fp, crdb::Database& db, const crdb::meta::DatabaseTypes& dbtypes)
	{
		// Create a big enough table dest
		int table_size = Read<int>(fp);
		std::vector<TYPE> table(table_size);

		if (table_size)
		{
			// Allocate enough memory to store the entire table in packed binary format and read it from the file
			const crdb::meta::DatabaseType& type = dbtypes.GetType<TYPE>();
			int packed_size = table_size * type.packed_size;
			char* data = new char[packed_size];
			fread(data, packed_size, 1, fp);

			// Unpack the binary table
			UnpackTable(db, table, type, data);
			delete [] data;

			// Add to the database
			for (size_t i = 0; i < table.size(); i++)
			{
				db.AddPrimitive(table[i]);
			}
		}
	}


	void ReadNameTable(FILE* fp, crdb::Database& db)
	{
		// Read the table header
		int nb_names = Read<int>(fp);

		// Read and populate each name
		for (int i = 0; i < nb_names; i++)
		{
			crdb::u32 hash = Read<crdb::u32>(fp);
			std::string str = Read<std::string>(fp);
			db.m_Names[hash] = crdb::Name(hash, str);
		}
	}


	void ReadTextAttributeTable(FILE* fp, const crdb::Database& db)
	{
		// Read the table header
		int nb_text_attributes = Read<int>(fp);

		// Read and populate the hash map
		g_TextAttributeMap.clear();
		for (int i = 0; i < nb_text_attributes; i++)
		{
			crdb::u32 hash = Read<crdb::u32>(fp);
			std::string text = Read<std::string>(fp);
			g_TextAttributeMap[hash] = text;
		}
	}
}


bool crdb::ReadBinaryDatabase(const char* filename, Database& db)
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
	crdb::meta::DatabaseTypes dbtypes;
	ReadNameTable(fp, db);
	ReadTextAttributeTable(fp, db);
	ReadTable<crdb::Type>(fp, db, dbtypes);
	ReadTable<crdb::EnumConstant>(fp, db, dbtypes);
	ReadTable<crdb::Enum>(fp, db, dbtypes);
	ReadTable<crdb::Field>(fp, db, dbtypes);
	ReadTable<crdb::Function>(fp, db, dbtypes);
	ReadTable<crdb::Class>(fp, db, dbtypes);
	ReadTable<crdb::Template>(fp, db, dbtypes);
	ReadTable<crdb::TemplateType>(fp, db, dbtypes);
	ReadTable<crdb::Namespace>(fp, db, dbtypes);

	// Read attribute tables with explicit ordering
	ReadTable<crdb::FlagAttribute>(fp, db, dbtypes);
	ReadTable<crdb::IntAttribute>(fp, db, dbtypes);
	ReadTable<crdb::FloatAttribute>(fp, db, dbtypes);
	ReadTable<crdb::NameAttribute>(fp, db, dbtypes);
	ReadTable<crdb::TextAttribute>(fp, db, dbtypes);

	fclose(fp);

	return true;
}


bool crdb::IsBinaryDatabase(const char* filename)
{
	// Not a database if the file can't be found
	FILE* fp = fopen(filename, "rb");
	if (fp == 0)
	{
		fclose(fp);
	}

	// Read the header and check it
	unsigned int header = Read<unsigned int>(fp);
	unsigned int version = Read<unsigned int>(fp);
	bool is_binary_db = header == FILE_HEADER && version == FILE_VERSION;

	fclose(fp);
	return is_binary_db;
}