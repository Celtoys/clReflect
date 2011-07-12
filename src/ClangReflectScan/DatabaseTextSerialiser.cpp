
#include "DatabaseTextSerialiser.h"
#include "Database.h"

#include <stdio.h>


namespace
{
	//
	// b64 encode/decode block taken from http://base64.sourceforge.net/ by Bob Trower
	// Copyright (c) Trantor Standard Systems Inc., 2001
	// Under the MIT license
	//


	/*
	** Translation Table as described in RFC1113
	*/
	static const char cb64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


	/*
	** Translation Table to decode (created by author)
	*/
	static const char cd64[] = "|$$$}rstuvwxyz{$$$$$$$>?@ABCDEFGHIJKLMNOPQRSTUVW$$$$$$XYZ[\\]^_`abcdefghijklmnopq";


	/*
	** encodeblock
	**
	** encode 3 8-bit binary bytes as 4 '6-bit' characters
	*/
	void encodeblock(unsigned char in[3], char out[4], int len)
	{
		out[0] = cb64[in[0] >> 2];
		out[1] = cb64[((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4)];
		out[2] = len > 1 ? cb64[ ((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6) ] : '=';
		out[3] = len > 2 ? cb64[ in[2] & 0x3f ] : '=';
	}


	/*
	** decodeblock
	**
	** decode 4 '6-bit' characters into 3 8-bit binary bytes
	*/
	void decodeblock(const char in[4], unsigned char out[3])
	{
		out[0] = (unsigned char)(in[0] << 2 | in[1] >> 4);
		out[1] = (unsigned char)(in[1] << 4 | in[2] >> 2);
		out[2] = (unsigned char)(((in[2] << 6) & 0xc0) | in[3]);
	}


	const char* b64StringFromName(crdb::Name name, const crdb::Database& db)
	{
		static char name_b64[8];

		// Safely handle no-names
		crdb::u32 name_u32 = 0;
		if (name != db.GetNoName())
		{
			name_u32 = name->first;
		}

		// Encode 4 bytes as 6 readable bytes
		crdb::u8* name_b256 = (crdb::u8*)&name_u32;
		encodeblock(name_b256 + 0, name_b64, 3);
		encodeblock(name_b256 + 3, name_b64 + 4, 1);
		name_b64[6] = 0;

		return name_b64;
	}


	const char* itoa64(__int64 value)
	{
		static const int MAX_SZ = 20;
		static char text[MAX_SZ];

		// Null terminate and start at the end
		text[MAX_SZ - 1] = 0;
		char* tptr = text + MAX_SZ - 1;

		// Get the absolute and record if the value is negative
		bool negative = false;
		if (value < 0)
		{
			negative = true;
			value = -value;
		}

		// Loop through the value with radix 10
		do 
		{
			int v = value % 10;
			*--tptr = '0' + v;
			value /= 10;
		} while (value);

		if (negative)
		{
			*--tptr = '-';
		}

		return tptr;
	}


	void WriteNamedRuler(FILE* fp, const char* text)
	{
		// Overwrite the '-' character with any text to keep the ruler width consistent
		char ruler[] = "---- --------------------------------------------------------------------\n";
		strcpy(ruler + 5, text);
		ruler[5 + strlen(text)] = ' ';
		fputs(ruler, fp);
	}


	void WriteRuler(FILE* fp)
	{
		fputs("-------------------------------------------------------------------------\n", fp);
	}


	void WriteTableHeader(FILE* fp, const char* title, bool named, const char* headers)
	{
		// Postfix the title with the named property
		char full_title[256];
		strcpy(full_title, title);
		if (named)
		{
			strcat(full_title, " (named)");
		}

		// Skip over the "Name" header if this is an unnamed table
		if (!named && strstr(headers, "Name\t") == headers)
		{
			headers += strlen("Name\t");
		}

		WriteNamedRuler(fp, full_title);
		fputs(headers, fp);
		fputs("\n", fp);
		WriteRuler(fp);
	}


	void WriteTableFooter(FILE* fp)
	{
		WriteRuler(fp);
		fputs("\n\n", fp);
	}


	void WritePrimitive(FILE* fp, const crdb::Primitive& primitive, bool named, const crdb::Database& db)
	{
		if (named)
		{
			fputs(b64StringFromName(primitive.name, db), fp);
			fputs("\t", fp);
		}
		fputs(b64StringFromName(primitive.parent, db), fp);
	}


	void WriteClass(FILE* fp, const crdb::Class& primitive, bool named, const crdb::Database& db)
	{
		WritePrimitive(fp, primitive, named, db);
		fputs("\t", fp);
		fputs(b64StringFromName(primitive.base_class, db), fp);
	}

	
	void WriteEnumConstant(FILE* fp, const crdb::EnumConstant& primitive, bool named, const crdb::Database& db)
	{
		WritePrimitive(fp, primitive, named, db);
		fputs("\t", fp);
		fputs(itoa64(primitive.value), fp);
	}


	void WriteField(FILE* fp, const crdb::Field& primitive, bool named, const crdb::Database& db)
	{
		WritePrimitive(fp, primitive, named, db);
		fputs("\t", fp);
		fputs(b64StringFromName(primitive.type, db), fp);
		fputs("\t", fp);

		switch (primitive.modifier)
		{
		case (crdb::Field::MODIFIER_VALUE): fputs("v", fp); break;
		case (crdb::Field::MODIFIER_POINTER): fputs("p", fp); break;
		case (crdb::Field::MODIFIER_REFERENCE): fputs("r", fp); break;
		}

		fputs(primitive.is_const ? "\t1" : "\t0", fp);

		fputs("\t", fp);
		fputs(itoa64(primitive.index), fp);
	}


	template <typename TYPE, typename PRINT_FUNC>
	void WritePrimitives(FILE* fp, const crdb::Database& db, PRINT_FUNC print_func, const char* title, const char* headers)
	{
		// Map from the type to the DB store
		const crdb::PrimitiveStore<TYPE>& store = db.GetPrimitiveStore<TYPE>();

		if (!store.named.empty())
		{
			// Write each named primitive
			WriteTableHeader(fp, title, true, headers);
			for (crdb::PrimitiveStore<TYPE>::NamedStore::const_iterator i = store.named.begin(); i != store.named.end(); ++i)
			{
				const TYPE& primitive = i->second;
				print_func(fp, primitive, true, db);
				fputs("\n", fp);
			}
			WriteTableFooter(fp);
		}

		if (!store.unnamed.empty())
		{
			// Write each unnamed primitive
			WriteTableHeader(fp, title, false, headers);
			for (crdb::PrimitiveStore<TYPE>::UnnamedStore::const_iterator i = store.unnamed.begin(); i != store.unnamed.end(); ++i)
			{
				const TYPE& primitive = *i;
				print_func(fp, primitive, false, db);
				fputs("\n", fp);
			}
			WriteTableFooter(fp);
		}
	}
}


void crdb::WriteTextDatabase(const char* filename, const Database& db)
{
	FILE* fp = fopen(filename, "w");

	// Write the name table
	WriteTableHeader(fp, "Names", false, "Hash\tName");
	for (NameMap::const_iterator i = db.m_Names.begin(); i != db.m_Names.end(); ++i)
	{
		fputs(b64StringFromName(i, db), fp);
		fputs("\t", fp);
		fputs(i->second.c_str(), fp);
		fputs("\n", fp);
	}
	WriteTableFooter(fp);

	// Write all the primitive tables
	WritePrimitives<Namespace>(fp, db, WritePrimitive, "Namespaces", "Name\tParent");
	WritePrimitives<Type>(fp, db, WritePrimitive, "Types", "Name\tParent");
	WritePrimitives<Class>(fp, db, WriteClass, "Classes", "Name\tParent\tBase");
	WritePrimitives<Enum>(fp, db, WritePrimitive, "Enums", "Name\tParent");
	WritePrimitives<EnumConstant>(fp, db, WriteEnumConstant, "Enum Constants", "Name\tParent\tValue");
	WritePrimitives<Function>(fp, db, WritePrimitive, "Functions", "Name\tParent");
	WritePrimitives<Field>(fp, db, WriteField, "Fields", "Name\tParent\tType\tMod\tCst\tIdx");

	fclose(fp);
}