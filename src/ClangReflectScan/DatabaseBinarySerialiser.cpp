
#include "DatabaseBinarySerialiser.h"
#include "Database.h"
#include "DatabaseMetadata.h"

#include <vector>


namespace
{
	template <typename TYPE, int SIZE>
	void CopyInteger(const crdb::Database&, char* dest, const char* source, int)
	{
		// Ensure the assumed size is the same as the machine size
		int assert_size_is_correct[sizeof(TYPE) == SIZE];
		(void)assert_size_is_correct;

		// Quick assign copy
		*(TYPE*)dest = *(TYPE*)source;
	}


	void MemCopy(const crdb::Database&, char* dest, const char* source, int size)
	{
		memcpy(dest, source, size);
	}


	void CopyName(const crdb::Database& db, char* dest, const char* source, int)
	{
		// Copy the hash
		crdb::Name& name = *(crdb::Name*)source;
		*(crdb::u32*)dest = name == db.GetNoName() ? 0 : name->first;
	}


	template <void PACK_FUNC(const crdb::Database&, char*, const char*, int)>
	void PackStridedData(const crdb::Database& db, char* dest, const char* source, int nb_entries, int dest_stride, int source_stride, int field_size)
	{
		// The compiler should be able to inline the call the PACK_FUNC for each entry
		for (int i = 0; i < nb_entries; i++)
		{
			PACK_FUNC(db, dest, source, field_size);
			dest += dest_stride;
			source += source_stride;
		}
	}


	void PackBasicFields(const crdb::Database& db, char* dest, const char* source, int nb_entries, const crdb::meta::DatabaseType& type, const crdb::meta::DatabaseField& field)
	{
		// Use memcpy as a last resort - try at least to use some big machine-size types
		switch (field.size)
		{
		case (1): PackStridedData< CopyInteger<bool, 1> >(db, dest, source, nb_entries, type.packed_size, type.size, 0); break;
		case (2): PackStridedData< CopyInteger<short, 2> >(db, dest, source, nb_entries, type.packed_size, type.size, 0); break;
		case (4): PackStridedData< CopyInteger<int, 4> >(db, dest, source, nb_entries, type.packed_size, type.size, 0); break;
		case (8): PackStridedData< CopyInteger<__int64, 8> >(db, dest, source, nb_entries, type.packed_size, type.size, 0); break;
		default: PackStridedData< MemCopy >(db, dest, source, nb_entries, type.packed_size, type.size, field.size); break;
		}
	}


	void PackNameFields(const crdb::Database& db, char* dest, const char* source, int nb_entries, const crdb::meta::DatabaseType& type, const crdb::meta::DatabaseField& field)
	{
		PackStridedData< CopyName >(db, dest, source, nb_entries, type.packed_size, type.size, 0);
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
				char* source = (char*)&table.front() + field.offset;

				// Perform strided copies depending on field type - pass information about the root type
				switch (field.type)
				{
				case (crdb::meta::FIELD_TYPE_BASIC): PackBasicFields(db, dest, source, table.size(), type, field); break;
				case (crdb::meta::FIELD_TYPE_NAME): PackNameFields(db, dest, source, table.size(), type, field); break;
				}
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
		fwrite(&table_size, sizeof(int), 1, fp);

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
}


void crdb::WriteBinaryDatabase(const char* filename, const Database& db)
{
	FILE* fp = fopen(filename, "wb");

	crdb::meta::DatabaseTypes dbtypes;
	WriteTable<crdb::Namespace>(fp, db, dbtypes);
	WriteTable<crdb::Type>(fp, db, dbtypes);
	WriteTable<crdb::Class>(fp, db, dbtypes);
	WriteTable<crdb::Enum>(fp, db, dbtypes);
	WriteTable<crdb::EnumConstant>(fp, db, dbtypes);
	WriteTable<crdb::Function>(fp, db, dbtypes);
	WriteTable<crdb::Field>(fp, db, dbtypes);

	fclose(fp);
}