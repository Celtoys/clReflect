
#include "DatabaseBinarySerialiser.h"
#include "Database.h"
#include "DatabaseMetadata.h"

#include <vector>


namespace
{
	// 'crdb'
	const unsigned int FILE_HEADER = 0x62647263;
	const unsigned int FILE_VERSION = 1;


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
		// Strip the has from the name
		crdb::Name& name = *(crdb::Name*)source;
		*(crdb::u32*)dest = name == db.GetNoName() ? 0 : name->first;
	}


	void CopyHashToName(const crdb::Database& db, char* dest, const char* source, int)
	{
		// Write the name as looked up by the hash
		crdb::u32 hash = *(crdb::u32*)source;
		*(crdb::Name*)dest = db.GetName(hash);
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
		case (8): CopyStridedData< CopyInteger<__int64, 8> >(db, dest, source, nb_entries, dest_stride, source_stride, field_size); break;
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

				// Start at the offset from the field within the first object
				char* dest = output + field.packed_offset;
				const char* source = (char*)&table.front() + field.offset;

				// Perform strided copies depending on field type - pass information about the root type
				switch (field.type)
				{
				case (crdb::meta::FIELD_TYPE_BASIC): CopyBasicFields(db, dest, source, table.size(), type.packed_size, type.size, field.size); break;
				case (crdb::meta::FIELD_TYPE_NAME): CopyStridedData<CopyNameToHash>(db, dest, source, table.size(), type.packed_size, type.size, field.size); break;
				}
			}
		}
	}


	template <typename TYPE>
	void CopyPrimitiveStoreToTable(const crdb::PrimitiveStore<TYPE>& store, std::vector<TYPE>& table, bool named)
	{
		int dest_index = 0;

		// Make a local copy of all entries in the table
		if (named)
		{
			table.resize(store.named.size());
			for (crdb::PrimitiveStore<TYPE>::NamedStore::const_iterator i = store.named.begin(); i != store.named.end(); ++i)
			{
				table[dest_index++] = i->second;
			}
		}
		else
		{
			table.resize(store.unnamed.size());
			for (crdb::PrimitiveStore<TYPE>::UnnamedStore::const_iterator i = store.unnamed.begin(); i != store.unnamed.end(); ++i)
			{
				table[dest_index++] = *i;
			}
		}
	}


	template <typename TYPE>
	void WriteTable(FILE* fp, const crdb::Database& db, const crdb::meta::DatabaseTypes& dbtypes, bool named)
	{
		// Generate a memory-contiguous table
		std::vector<TYPE> table;
		CopyPrimitiveStoreToTable(db.GetPrimitiveStore<TYPE>(), table, named);

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


	template <typename TYPE>
	void WriteTable(FILE* fp, const crdb::Database& db, const crdb::meta::DatabaseTypes& dbtypes)
	{
		// Write both named and unnamed tables
		// The unnamed tables contain the empty names, but this makes the code much simpler
		// at the expense of file sizes that are little larger
		WriteTable<TYPE>(fp, db, dbtypes, true);
		WriteTable<TYPE>(fp, db, dbtypes, false);
	}


	void WriteNameTable(FILE* fp, const crdb::Database& db)
	{
		// Write the table header
		int nb_names = db.m_Names.size();
		Write(fp, nb_names);

		// Write each name
		for (crdb::Name i = db.m_Names.begin(); i != db.m_Names.end(); ++i)
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
	WriteTable<crdb::Namespace>(fp, db, dbtypes);
	WriteTable<crdb::Type>(fp, db, dbtypes);
	WriteTable<crdb::Class>(fp, db, dbtypes);
	WriteTable<crdb::Enum>(fp, db, dbtypes);
	WriteTable<crdb::EnumConstant>(fp, db, dbtypes);
	WriteTable<crdb::Function>(fp, db, dbtypes);
	WriteTable<crdb::Field>(fp, db, dbtypes);

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


	void ReadNameTable(FILE* fp, crdb::Database& db)
	{
		// Read the table header
		int nb_names = Read<int>(fp);

		// Read and populate each name
		for (int i = 0; i < nb_names; i++)
		{
			crdb::u32 hash = Read<crdb::u32>(fp);
			std::string str = Read<std::string>(fp);
			db.m_Names[hash] = str;
		}
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

				// Start at the offset from the field within the first object
				char* dest = (char*)&table.front() + field.offset;
				const char* source = input + field.packed_offset;

				// Perform strided copies depending on field type - pass information about the root type
				switch (field.type)
				{
				case (crdb::meta::FIELD_TYPE_BASIC): CopyBasicFields(db, dest, source, table.size(), type.size, type.packed_size, field.size); break;
				case (crdb::meta::FIELD_TYPE_NAME): CopyStridedData<CopyHashToName>(db, dest, source, table.size(), type.size, type.packed_size, field.size); break;
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


	template <typename TYPE>
	void ReadTables(FILE* fp, crdb::Database& db, const crdb::meta::DatabaseTypes& dbtypes)
	{
		// Read both named and unnamed tables Database::AddPrimitive automatically figures
		// out which primitive store to add to
		ReadTable<TYPE>(fp, db, dbtypes);
		ReadTable<TYPE>(fp, db, dbtypes);
	}
}


void crdb::ReadBinaryDatabase(const char* filename, Database& db)
{
	FILE* fp = fopen(filename, "rb");

	// Read the header and check it
	unsigned int header = Read<unsigned int>(fp);
	unsigned int version = Read<unsigned int>(fp);
	if (header != FILE_HEADER || version != FILE_VERSION)
	{
		fclose(fp);
		return;
	}

	// Read each table with explicit ordering
	crdb::meta::DatabaseTypes dbtypes;
	ReadNameTable(fp, db);
	ReadTables<crdb::Namespace>(fp, db, dbtypes);
	ReadTables<crdb::Type>(fp, db, dbtypes);
	ReadTables<crdb::Class>(fp, db, dbtypes);
	ReadTables<crdb::Enum>(fp, db, dbtypes);
	ReadTables<crdb::EnumConstant>(fp, db, dbtypes);
	ReadTables<crdb::Function>(fp, db, dbtypes);
	ReadTables<crdb::Field>(fp, db, dbtypes);

	fclose(fp);
}