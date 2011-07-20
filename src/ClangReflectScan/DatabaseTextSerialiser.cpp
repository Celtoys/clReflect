
#include "DatabaseTextSerialiser.h"
#include "Database.h"

#include <stdio.h>


namespace
{
	// Serialisation version
	const int CURRENT_VERSION = 1;


	bool startswith(const char* text, const char* cmp)
	{
		return strstr(text, cmp) == text;
	}


	// Enough for 32-bit and 64-bit values
	template <typename TYPE>
	const char* itoa(TYPE value)
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


	__int64 atoi64(const char* text)
	{
		// Skip the negative notation
		bool negative = false;
		if (*text == '-')
		{
			negative = true;
			text++;
		}

		// Sum each radix 10 element
		__int64 val = 0;
		for (const char* tptr = text, *end = text + strlen(text); tptr != end; ++tptr)
		{
			val *= 10;
			int v = *tptr - '0';
			val += v;
		}

		// Negate if necessary
		if (negative)
		{
			val = -val;
		}

		return val;
	}


	const char* itohex(crdb::u32 value)
	{
		static const int MAX_SZ = 9;
		static char text[MAX_SZ];

		// Null terminate and start at the end
		text[MAX_SZ - 1] = 0;
		char* tptr = text + MAX_SZ - 1;

		// Loop through the value with radix 16
		do 
		{
			int v = value & 15;
			*--tptr = "0123456789abcdef"[v];
			value /= 16;
		} while (value);

		// Zero-pad whats left
		while (tptr != text)
		{
			*--tptr = '0';
		}

		return tptr;
	}


	crdb::u32 hextoi(const char* text)
	{
		// Sum each radix 16 element
		crdb::u32 val = 0;
		for (const char* tptr = text, *end = text + strlen(text); tptr != end; ++tptr)
		{
			val *= 16;
			int v = *tptr >= 'a' ? *tptr - 'a' + 10 : *tptr - '0';
			val += v;
		}

		return val;
	}


	const char* HexStringFromName(crdb::Name name, const crdb::Database& db)
	{
		// Safely handle no-names
		crdb::u32 hash = 0;
		if (name != db.GetNoName())
		{
			hash = name->first;
		}

		return itohex(hash);
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
		if (!named && startswith(headers, "Name\t\t"))
		{
			headers += strlen("Name\t\t");
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


	void WriteName(FILE* fp, const crdb::NameMap::value_type& name, bool named, const crdb::Database& db)
	{
		fputs(itohex(name.first), fp);
		fputs("\t", fp);
		fputs(name.second.c_str(), fp);
	}


	void WritePrimitive(FILE* fp, const crdb::Primitive& primitive, bool named, const crdb::Database& db)
	{
		if (named)
		{
			fputs(HexStringFromName(primitive.name, db), fp);
			fputs("\t", fp);
		}
		fputs(HexStringFromName(primitive.parent, db), fp);
	}


	void WriteClass(FILE* fp, const crdb::Class& primitive, bool named, const crdb::Database& db)
	{
		WritePrimitive(fp, primitive, named, db);
		fputs("\t", fp);
		fputs(HexStringFromName(primitive.base_class, db), fp);
		fputs("\t", fp);
		fputs(itohex(primitive.size), fp);
	}

	
	void WriteEnumConstant(FILE* fp, const crdb::EnumConstant& primitive, bool named, const crdb::Database& db)
	{
		WritePrimitive(fp, primitive, named, db);
		fputs("\t", fp);
		fputs(itoa(primitive.value), fp);
	}


	void WriteFunction(FILE* fp, const crdb::Function& primitive, bool named, const crdb::Database& db)
	{
		WritePrimitive(fp, primitive, named, db);
		fputs("\t", fp);
		fputs(itohex(primitive.unique_id), fp);
	}


	void WriteField(FILE* fp, const crdb::Field& primitive, bool named, const crdb::Database& db)
	{
		WritePrimitive(fp, primitive, named, db);
		fputs("\t", fp);
		fputs(HexStringFromName(primitive.type, db), fp);
		fputs("\t", fp);

		switch (primitive.modifier)
		{
		case (crdb::Field::MODIFIER_VALUE): fputs("v", fp); break;
		case (crdb::Field::MODIFIER_POINTER): fputs("p", fp); break;
		case (crdb::Field::MODIFIER_REFERENCE): fputs("r", fp); break;
		}

		fputs(primitive.is_const ? "\t1" : "\t0", fp);

		fputs("\t", fp);
		fputs(itoa(primitive.offset), fp);
		fputs("\t\t", fp);
		fputs(itohex(primitive.parent_unique_id), fp);
	}


	template <typename TABLE_TYPE, typename PRINT_FUNC>
	void WriteTable(FILE* fp, const crdb::Database& db, const TABLE_TYPE& table, PRINT_FUNC print_func, bool named, const char* title, const char* headers)
	{
		WriteTableHeader(fp, title, named, headers);
		for (typename TABLE_TYPE::const_iterator i = table.begin(); i != table.end(); ++i)
		{
			print_func(fp, i->second, named, db);
			fputs("\n", fp);
		}
		WriteTableFooter(fp);
	}


	template <typename TYPE, typename PRINT_FUNC>
	void WritePrimitives(FILE* fp, const crdb::Database& db, PRINT_FUNC print_func, bool named, const char* title, const char* headers)
	{
		if (named)
		{
			const crdb::PrimitiveStore<TYPE>& store = db.GetPrimitiveStore<TYPE>();
			WriteTable(fp, db, store, print_func, named, title, headers);
		}
		else
		{
			const crdb::PrimitiveStore<TYPE>& store = db.GetUnnamedPrimitiveStore<TYPE>();
			WriteTable(fp, db, store, print_func, named, title, headers);
		}
	}


	void WriteNameTable(FILE* fp, const crdb::Database& db, const crdb::NameMap& table)
	{
		WriteTableHeader(fp, "Names", true, "Hash\t\tName");
		for (crdb::NameMap::const_iterator i = table.begin(); i != table.end(); ++i)
		{
			WriteName(fp, *i, true, db);
			fputs("\n", fp);
		}
		WriteTableFooter(fp);
	}
}


void crdb::WriteTextDatabase(const char* filename, const Database& db){
	FILE* fp = fopen(filename, "w");

	// Write the header
	fputs("\nClang Reflect Database\n", fp);
	fputs("Format Version: ", fp);
	fputs(itoa(CURRENT_VERSION), fp);
	fputs("\n\n\n", fp);

	// Write the name table
	WriteNameTable(fp, db, db.m_Names);

	// Write all the primitive tables
	WritePrimitives<Namespace>(fp, db, WritePrimitive, true, "Namespaces", "Name\t\tParent");
	WritePrimitives<Type>(fp, db, WritePrimitive, true, "Types", "Name\t\tParent");
	WritePrimitives<Class>(fp, db, WriteClass, true, "Classes", "Name\t\tParent\t\tBase\tSize");
	WritePrimitives<Enum>(fp, db, WritePrimitive, true, "Enums", "Name\t\tParent");
	WritePrimitives<EnumConstant>(fp, db, WriteEnumConstant, true, "Enum Constants", "Name\t\tParent\t\tValue");
	WritePrimitives<Function>(fp, db, WriteFunction, true, "Functions", "Name\t\tParent\t\tUID");
	WritePrimitives<Field>(fp, db, WriteField, true, "Fields", "Name\t\tParent\t\tType\t\tMod\tCst\tOffs\tUID");
	WritePrimitives<Field>(fp, db, WriteField, false, "Fields", "Parent\t\tType\t\tMod\tCst\tOffs\tUID");

	fclose(fp);
}


namespace
{
	//
	// Simple wrapper class around strtok that remembers the delimiter and automatically
	// continues where the last token parse left off.
	//
	class StringTokeniser
	{
	public:
		StringTokeniser(char* text, const char* delimiter)
			: m_Text(text)
			, m_Delimiter(delimiter)
		{
		}

		const char* Get()
		{
			const char* token = strtok(m_Text, m_Delimiter);
			m_Text = 0;
			return token;
		}

		// Helper for safely retrieving the next hex string token as an integer
		crdb::u32 GetHexInt()
		{
			const char* token = Get();
			if (token == 0)
			{
				return 0;
			}
			return hextoi(token);
		}

		// Automating the process of getting the common primitive data
		void GetNameAndParent(bool named, crdb::u32& name, crdb::u32& parent)
		{
			name = 0;
			if (named)
			{
				name = GetHexInt();
			}
			parent = GetHexInt();
		}

	private:
		char* m_Text;
		const char* m_Delimiter;
	};


	char* ReadLine(FILE* fp)
	{
		static char line[4096];

		// Loop reading characters until EOF or EOL
		int pos = 0;
		while (true)
		{
			int c = fgetc(fp);
			if (c == EOF)
			{
				return 0;
			}
			if (c == '\n')
			{
				break;
			}

			// Only add if the line is below the length of the buffer
			if (pos < sizeof(line) - 1)
			{
				line[pos++] = c;
			}
		}

		// Null terminate and return
		line[pos] = 0;
		return line;
	}


	void ParseName(char* line, crdb::Database& db, bool named)
	{
		StringTokeniser tok(line, "\t");
		crdb::u32 hash = tok.GetHexInt();
		const char* name = tok.Get();
		if (hash != 0 && name != 0)
		{
			db.m_Names[hash] = name;
		}
	}


	template <typename TYPE>
	void ParsePrimitive(char* line, crdb::Database& db, bool named)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(named, name, parent);
		
		// Add a new primitive to the database
		TYPE primitive(
			db.GetName(name),
			db.GetName(parent));

		db.AddPrimitive(primitive);
	}


	void ParseClass(char* line, crdb::Database& db, bool named)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(named, name, parent);

		// Class parsing
		crdb::u32 base = tok.GetHexInt();
		crdb::u32 size = tok.GetHexInt();

		// Add a new class to the database
		crdb::Class primitive(
			db.GetName(name),
			db.GetName(parent),
			db.GetName(base),
			size);

		db.AddPrimitive(primitive);
	}


	void ParseEnumConstant(char* line, crdb::Database& db, bool named)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(named, name, parent);

		// Enum constant parsing
		__int64 value = atoi64(tok.Get());

		// Add a new enum constant to the database
		crdb::EnumConstant primitive(
			db.GetName(name),
			db.GetName(parent),
			value);

		db.AddPrimitive(primitive);
	}


	void ParseFunction(char* line, crdb::Database& db, bool named)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(named, name, parent);

		// Function parsing
		crdb::u32 unique_id = tok.GetHexInt();

		// Add a new function to the database
		crdb::Function primitive(
			db.GetName(name),
			db.GetName(parent),
			unique_id);

		db.AddPrimitive(primitive);
	}


	void ParseField(char* line, crdb::Database& db, bool named)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(named, name, parent);

		// Field parsing

		crdb::u32 type = tok.GetHexInt();

		const char* mod = tok.Get();
		crdb::Field::Modifier modifier;
		switch (mod[0])
		{
		case ('v'): modifier = crdb::Field::MODIFIER_VALUE; break;
		case ('p'): modifier = crdb::Field::MODIFIER_POINTER; break;
		case ('r'): modifier = crdb::Field::MODIFIER_REFERENCE; break;
		}

		const char* cst = tok.Get();
		bool is_const = cst[0] != '0';

		const char* idx = tok.Get();
		int index = atoi(idx);

		crdb::u32 parent_unique_id = tok.GetHexInt();

		// Add a new field to the database
		crdb::Field primitive(
			db.GetName(name),
			db.GetName(parent),
			db.GetName(type),
			modifier,
			is_const,
			index,
			parent_unique_id);

		db.AddPrimitive(primitive);
	}


	template <typename PARSE_FUNC>
	void ParseTable(FILE* fp, char* line, crdb::Database& db, const char* table_name, PARSE_FUNC parse_func)
	{
		// Format the table header
		char table_header[256] = "---- ";
		strcat(table_header, table_name);
		strcat(table_header, " ");

		// Is this a named table?
		bool named = false;
		if (strstr(line, " (named) "))
		{
			named = true;
		}

		// See if this is the required table and consumer the header
		if (startswith(line, table_header))
		{
			if (ReadLine(fp) == 0)
			{
				return;
			}
			if (ReadLine(fp) == 0)
			{
				return;
			}

			// Loop reading all lines until the table completes
			while (char* subline = ReadLine(fp))
			{
				if (startswith(subline, "----"))
				{
					break;
				}

				parse_func(subline, db, named);
			}
		}
	}
}


void crdb::ReadTextDatabase(const char* filename, Database& db)
{
	FILE* fp = fopen(filename, "r");

	// Parse the tables in whatever order they arrive
	while (char* line = ReadLine(fp))
	{
		// Parse the header to see if the version is readable
		if (startswith(line, "Format Version: "))
		{
			StringTokeniser tok(line, ":");
			tok.Get();
			const char* version = tok.Get();
			if (atoi(version) != CURRENT_VERSION)
			{
				fclose(fp);
				return;
			}
		}

		ParseTable(fp, line, db, "Names", ParseName);
		ParseTable(fp, line, db, "Namespaces", ParsePrimitive<crdb::Namespace>);
		ParseTable(fp, line, db, "Types", ParsePrimitive<crdb::Type>);
		ParseTable(fp, line, db, "Classes", ParseClass);
		ParseTable(fp, line, db, "Enums", ParsePrimitive<crdb::Enum>);
		ParseTable(fp, line, db, "Enum Constants", ParseEnumConstant);
		ParseTable(fp, line, db, "Functions", ParseFunction);
		ParseTable(fp, line, db, "Fields", ParseField);
		ParseTable(fp, line, db, "Fields", ParseField);
	}

	fclose(fp);
}