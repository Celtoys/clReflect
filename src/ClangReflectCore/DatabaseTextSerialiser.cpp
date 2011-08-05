
// TODO: This entire serialisation strategy is very brittle - it's very easy to change the layout of the database
// while forgetting to update this correctly without making mistakes. Rewrite so that it uses the database metadata.

#include "DatabaseTextSerialiser.h"
#include "Database.h"
#include "FileUtils.h"

#include <stdio.h>


namespace
{
	// Serialisation version
	const int CURRENT_VERSION = 1;


	bool startswith(const char* text, const char* cmp)
	{
		return strstr(text, cmp) == text;
	}


	const char* HexStringFromName(crdb::Name name, const crdb::Database& db)
	{
		return itohex(name.hash);
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


	void WriteTableHeader(FILE* fp, const char* title, const char* headers)
	{
		WriteNamedRuler(fp, title);
		fputs(headers, fp);
		fputs("\n", fp);
		WriteRuler(fp);
	}


	void WriteTableFooter(FILE* fp)
	{
		WriteRuler(fp);
		fputs("\n\n", fp);
	}


	void WriteName(FILE* fp, const crdb::Name& name, const crdb::Database& db)
	{
		fputs(itohex(name.hash), fp);
		fputs("\t", fp);
		fputs(name.text.c_str(), fp);
	}


	void WritePrimitive(FILE* fp, const crdb::Primitive& primitive, const crdb::Database& db)
	{
		fputs(HexStringFromName(primitive.name, db), fp);
		fputs("\t", fp);
		fputs(HexStringFromName(primitive.parent, db), fp);
	}


	void WriteType(FILE* fp, const crdb::Type& primitive, const crdb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);
		fputs(itohex(primitive.size), fp);
	}


	void WriteClass(FILE* fp, const crdb::Class& primitive, const crdb::Database& db)
	{
		WriteType(fp, primitive, db);
		fputs("\t", fp);
		fputs(HexStringFromName(primitive.base_class, db), fp);
	}

	
	void WriteEnumConstant(FILE* fp, const crdb::EnumConstant& primitive, const crdb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);
		fputs(itoa(primitive.value), fp);
	}


	void WriteFunction(FILE* fp, const crdb::Function& primitive, const crdb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);
		fputs(itohex(primitive.unique_id), fp);
	}


	void WriteField(FILE* fp, const crdb::Field& primitive, const crdb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
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
	void WriteTable(FILE* fp, const crdb::Database& db, const TABLE_TYPE& table, PRINT_FUNC print_func, const char* title, const char* headers)
	{
		WriteTableHeader(fp, title, headers);
		for (typename TABLE_TYPE::const_iterator i = table.begin(); i != table.end(); ++i)
		{
			print_func(fp, i->second, db);
			fputs("\n", fp);
		}
		WriteTableFooter(fp);
	}


	template <typename TYPE, typename PRINT_FUNC>
	void WritePrimitives(FILE* fp, const crdb::Database& db, PRINT_FUNC print_func, const char* title, const char* headers)
	{
		const crdb::PrimitiveStore<TYPE>& store = db.GetPrimitiveStore<TYPE>();
		WriteTable(fp, db, store, print_func, title, headers);
	}


	void WriteNameTable(FILE* fp, const crdb::Database& db, const crdb::NameMap& table)
	{
		WriteTableHeader(fp, "Names", "Hash\t\tName");
		for (crdb::NameMap::const_iterator i = table.begin(); i != table.end(); ++i)
		{
			WriteName(fp, i->second, db);
			fputs("\n", fp);
		}
		WriteTableFooter(fp);
	}
}


void crdb::WriteTextDatabase(const char* filename, const Database& db)
{
	FILE* fp = fopen(filename, "w");

	// Write the header
	fputs("\nClang Reflect Database\n", fp);
	fputs("Format Version: ", fp);
	fputs(itoa(CURRENT_VERSION), fp);
	fputs("\n\n\n", fp);

	// Write the name table
	WriteNameTable(fp, db, db.m_Names);

	// Write all the primitive tables
	WritePrimitives<Namespace>(fp, db, WritePrimitive, "Namespaces", "Name\t\tParent");
	WritePrimitives<Type>(fp, db, WriteType, "Types", "Name\t\tParent\t\tSize");
	WritePrimitives<Class>(fp, db, WriteClass, "Classes", "Name\t\tParent\t\tSize\t\tBase");
	WritePrimitives<Enum>(fp, db, WriteType, "Enums", "Name\t\tParent\t\tSize");
	WritePrimitives<EnumConstant>(fp, db, WriteEnumConstant, "Enum Constants", "Name\t\tParent\t\tValue");
	WritePrimitives<Function>(fp, db, WriteFunction, "Functions", "Name\t\tParent\t\tUID");
	WritePrimitives<Field>(fp, db, WriteField, "Fields", "Name\t\tParent\t\tType\t\tMod\tCst\tOffs\tUID");

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
		void GetNameAndParent(crdb::u32& name, crdb::u32& parent)
		{
			name = GetHexInt();
			parent = GetHexInt();
		}

	private:
		char* m_Text;
		const char* m_Delimiter;
	};


	void ParseName(char* line, crdb::Database& db)
	{
		StringTokeniser tok(line, "\t");
		crdb::u32 hash = tok.GetHexInt();
		const char* name = tok.Get();
		if (hash != 0 && name != 0)
		{
			db.m_Names[hash] = crdb::Name(hash, name);
		}
	}


	template <typename TYPE>
	void ParsePrimitive(char* line, crdb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(name, parent);
		
		// Add a new primitive to the database
		TYPE primitive(
			db.GetName(name),
			db.GetName(parent));

		db.AddPrimitive(primitive);
	}


	void ParseType(char* line, crdb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Type parsing
		crdb::u32 size = tok.GetHexInt();

		// Add a new primitive to the database
		crdb::Type primitive(
			db.GetName(name),
			db.GetName(parent),
			size);

		db.AddPrimitive(primitive);
	}


	void ParseClass(char* line, crdb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Type parsing
		crdb::u32 size = tok.GetHexInt();

		// Class parsing
		crdb::u32 base = tok.GetHexInt();

		// Add a new class to the database
		crdb::Class primitive(
			db.GetName(name),
			db.GetName(parent),
			db.GetName(base),
			size);

		db.AddPrimitive(primitive);
	}


	void ParseEnum(char* line, crdb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Type parsing - discard the size
		tok.GetHexInt();

		// Add a new class to the database
		crdb::Enum primitive(
			db.GetName(name),
			db.GetName(parent));

		db.AddPrimitive(primitive);
	}


	void ParseEnumConstant(char* line, crdb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Enum constant parsing
		int value = atoi(tok.Get());

		// Add a new enum constant to the database
		crdb::EnumConstant primitive(
			db.GetName(name),
			db.GetName(parent),
			value);

		db.AddPrimitive(primitive);
	}


	void ParseFunction(char* line, crdb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Function parsing
		crdb::u32 unique_id = tok.GetHexInt();

		// Add a new function to the database
		crdb::Function primitive(
			db.GetName(name),
			db.GetName(parent),
			unique_id);

		db.AddPrimitive(primitive);
	}


	void ParseField(char* line, crdb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		crdb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

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

				parse_func(subline, db);
			}
		}
	}
}


bool crdb::ReadTextDatabase(const char* filename, Database& db)
{
	if (!IsTextDatabase(filename))
	{
		return false;
	}

	FILE* fp = fopen(filename, "r");

	// Parse the tables in whatever order they arrive
	while (char* line = ReadLine(fp))
	{
		ParseTable(fp, line, db, "Names", ParseName);
		ParseTable(fp, line, db, "Namespaces", ParsePrimitive<crdb::Namespace>);
		ParseTable(fp, line, db, "Types", ParseType);
		ParseTable(fp, line, db, "Classes", ParseClass);
		ParseTable(fp, line, db, "Enums", ParseEnum);
		ParseTable(fp, line, db, "Enum Constants", ParseEnumConstant);
		ParseTable(fp, line, db, "Functions", ParseFunction);
		ParseTable(fp, line, db, "Fields", ParseField);
	}

	fclose(fp);
	return true;
}


bool crdb::IsTextDatabase(const char* filename)
{
	// Not a database if it doesn't exist
	FILE* fp = fopen(filename, "r");
	if (fp == 0)
	{
		return false;
	}

	// Parse the first few lines looking for the header
	int line_index = 0;
	bool is_text_db = true;
	while (char* line = ReadLine(fp))
	{
		if (startswith(line, "Clang Reflect Database"))
		{
			is_text_db = true;
		}

		// See if the version is readable
		if (is_text_db && startswith(line, "Format Version: "))
		{
			StringTokeniser tok(line, ":");
			tok.Get();
			const char* version = tok.Get();
			if (atoi(version) != CURRENT_VERSION)
			{
				is_text_db = false;
			}

			break;
		}

		if (line_index++ > 5)
		{
			break;
		}
	}

	fclose(fp);
	return is_text_db;
}