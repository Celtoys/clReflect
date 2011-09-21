
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011 Don Williamson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ===============================================================================
//

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


	const char* HexStringFromName(cldb::Name name, const cldb::Database& db)
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


	void WriteName(FILE* fp, const cldb::Name& name, const cldb::Database& db)
	{
		fputs(itohex(name.hash), fp);
		fputs("\t", fp);
		fputs(name.text.c_str(), fp);
	}


	void WritePrimitive(FILE* fp, const cldb::Primitive& primitive, const cldb::Database& db)
	{
		fputs(HexStringFromName(primitive.name, db), fp);
		fputs("\t", fp);
		fputs(HexStringFromName(primitive.parent, db), fp);
	}


	void WriteType(FILE* fp, const cldb::Type& primitive, const cldb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);
		fputs(itohex(primitive.size), fp);
	}


	void WriteEnumConstant(FILE* fp, const cldb::EnumConstant& primitive, const cldb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);
		fputs(itoa(primitive.value), fp);
	}


	void WriteQualifier(FILE* fp, const cldb::Qualifier& qualifier)
	{
		switch (qualifier.op)
		{
		case (cldb::Qualifier::VALUE): fputs("v", fp); break;
		case (cldb::Qualifier::POINTER): fputs("p", fp); break;
		case (cldb::Qualifier::REFERENCE): fputs("r", fp); break;
		}

		fputs(qualifier.is_const ? "\t1" : "\t0", fp);
	}


	void WriteField(FILE* fp, const cldb::Field& primitive, const cldb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);
		fputs(HexStringFromName(primitive.type, db), fp);
		fputs("\t", fp);
		WriteQualifier(fp, primitive.qualifier);
		fputs("\t", fp);
		fputs(itoa(primitive.offset), fp);
		fputs("\t\t", fp);
		fputs(itohex(primitive.parent_unique_id), fp);
	}


	void WriteFunction(FILE* fp, const cldb::Function& primitive, const cldb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);
		fputs(itohex(primitive.unique_id), fp);
	}


	void WriteTemplateType(FILE* fp, const cldb::TemplateType& primitive, const cldb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);

		for (int i = 0; i < cldb::TemplateType::MAX_NB_ARGS; i++)
		{
			if (primitive.parameter_types[i].hash)
			{
				fputs(itohex(primitive.parameter_types[i].hash), fp);
				fputs(primitive.parameter_ptrs[i] ? "\t1" : "\t0", fp);
				fputs("\t", fp);
			}
		}
	}


	void WriteClass(FILE* fp, const cldb::Class& primitive, const cldb::Database& db)
	{
		WriteType(fp, primitive, db);
		fputs("\t", fp);
		fputs(HexStringFromName(primitive.base_class, db), fp);
	}


	void WriteIntAttribute(FILE* fp, const cldb::IntAttribute& primitive, const cldb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);
		fputs(itoa(primitive.value), fp);
	}


	void WriteFloatAttribute(FILE* fp, const cldb::FloatAttribute& primitive, const cldb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);
		fprintf(fp, "%f", primitive.value);
	}


	void WriteNameAttribute(FILE* fp, const cldb::NameAttribute& primitive, const cldb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);
		fputs(itohex(primitive.value.hash), fp);
	}


	void WriteTextAttribute(FILE* fp, const cldb::TextAttribute& primitive, const cldb::Database& db)
	{
		WritePrimitive(fp, primitive, db);
		fputs("\t", fp);
		fputs(primitive.value.c_str(), fp);
	}


	template <typename TABLE_TYPE, typename PRINT_FUNC>
	void WriteTable(FILE* fp, const cldb::Database& db, const TABLE_TYPE& table, PRINT_FUNC print_func, const char* title, const char* headers)
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
	void WritePrimitives(FILE* fp, const cldb::Database& db, PRINT_FUNC print_func, const char* title, const char* headers)
	{
		const cldb::PrimitiveStore<TYPE>& store = db.GetPrimitiveStore<TYPE>();
		WriteTable(fp, db, store, print_func, title, headers);
	}


	void WriteNameTable(FILE* fp, const cldb::Database& db, const cldb::NameMap& table)
	{
		WriteTableHeader(fp, "Names", "Hash\t\tName");
		for (cldb::NameMap::const_iterator i = table.begin(); i != table.end(); ++i)
		{
			WriteName(fp, i->second, db);
			fputs("\n", fp);
		}
		WriteTableFooter(fp);
	}
}


void cldb::WriteTextDatabase(const char* filename, const Database& db)
{
	FILE* fp = fopen(filename, "w");

	// Write the header
	fputs("\nclReflect Database\n", fp);
	fputs("Format Version: ", fp);
	fputs(itoa(CURRENT_VERSION), fp);
	fputs("\n\n\n", fp);

	// Write the name table
	WriteNameTable(fp, db, db.m_Names);

	// Write all the primitive tables
	WritePrimitives<Type>(fp, db, WriteType, "Types", "Name\t\tParent\t\tSize");
	WritePrimitives<EnumConstant>(fp, db, WriteEnumConstant, "Enum Constants", "Name\t\tParent\t\tValue");
	WritePrimitives<Enum>(fp, db, WriteType, "Enums", "Name\t\tParent\t\tSize");
	WritePrimitives<Field>(fp, db, WriteField, "Fields", "Name\t\tParent\t\tType\t\tMod\tCst\tOffs\tUID");
	WritePrimitives<Function>(fp, db, WriteFunction, "Functions", "Name\t\tParent\t\tUID");
	WritePrimitives<Class>(fp, db, WriteClass, "Classes", "Name\t\tParent\t\tSize\t\tBase");
	WritePrimitives<Template>(fp, db, WritePrimitive, "Templates", "Name\t\tParent");
	WritePrimitives<TemplateType>(fp, db, WriteTemplateType, "Template Types", "Name\t\tParent\t\tArgument type and pointer pairs");
	WritePrimitives<Namespace>(fp, db, WritePrimitive, "Namespaces", "Name\t\tParent");

	// Write the attribute tables
	WritePrimitives<FlagAttribute>(fp, db, WritePrimitive, "Flag Attributes", "Name\t\tParent");
	WritePrimitives<IntAttribute>(fp, db, WriteIntAttribute, "Int Attributes", "Name\t\tParent\t\tValue");
	WritePrimitives<FloatAttribute>(fp, db, WriteFloatAttribute, "Float Attributes", "Name\t\tParent\t\tValue");
	WritePrimitives<NameAttribute>(fp, db, WriteNameAttribute, "Name Attributes", "Name\t\tParent\t\tValue");
	WritePrimitives<TextAttribute>(fp, db, WriteTextAttribute, "Text Attributes", "Name\t\tParent\t\tValue");

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
		cldb::u32 GetHexInt()
		{
			const char* token = Get();
			if (token == 0)
			{
				return 0;
			}
			return hextoi(token);
		}

		// Automating the process of getting the common primitive data
		void GetNameAndParent(cldb::u32& name, cldb::u32& parent)
		{
			name = GetHexInt();
			parent = GetHexInt();
		}

	private:
		char* m_Text;
		const char* m_Delimiter;
	};


	void ParseName(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");
		cldb::u32 hash = tok.GetHexInt();
		const char* name = tok.Get();
		if (hash != 0 && name != 0)
		{
			db.m_Names[hash] = cldb::Name(hash, name);
		}
	}


	template <typename TYPE>
	void ParsePrimitive(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);
		
		// Add a new primitive to the database
		TYPE primitive(
			db.GetName(name),
			db.GetName(parent));

		db.AddPrimitive(primitive);
	}


	void ParseType(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Type parsing
		cldb::u32 size = tok.GetHexInt();

		// Add a new primitive to the database
		cldb::Type primitive(
			db.GetName(name),
			db.GetName(parent),
			size);

		db.AddPrimitive(primitive);
	}


	void ParseEnumConstant(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Enum constant parsing
		int value = atoi(tok.Get());

		// Add a new enum constant to the database
		cldb::EnumConstant primitive(
			db.GetName(name),
			db.GetName(parent),
			value);

		db.AddPrimitive(primitive);
	}


	void ParseEnum(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Type parsing - discard the size
		tok.GetHexInt();

		// Add a new class to the database
		cldb::Enum primitive(
			db.GetName(name),
			db.GetName(parent));

		db.AddPrimitive(primitive);
	}


	cldb::Qualifier ParseQualifier(StringTokeniser& tok)
	{
		const char* mod = tok.Get();
		cldb::Qualifier qualifier;
		switch (mod[0])
		{
		case ('v'): qualifier.op = cldb::Qualifier::VALUE; break;
		case ('p'): qualifier.op = cldb::Qualifier::POINTER; break;
		case ('r'): qualifier.op = cldb::Qualifier::REFERENCE; break;
		}

		const char* cst = tok.Get();
		qualifier.is_const = cst[0] != '0';

		return qualifier;
	}


	void ParseField(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Field parsing

		cldb::u32 type = tok.GetHexInt();
		cldb::Qualifier qualifier = ParseQualifier(tok);

		const char* idx = tok.Get();
		int index = atoi(idx);

		cldb::u32 parent_unique_id = tok.GetHexInt();

		// Add a new field to the database
		cldb::Field primitive(
			db.GetName(name),
			db.GetName(parent),
			db.GetName(type),
			qualifier,
			index,
			parent_unique_id);

		db.AddPrimitive(primitive);
	}


	void ParseFunction(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Function parsing
		cldb::u32 unique_id = tok.GetHexInt();

		// Add a new function to the database
		cldb::Function primitive(
			db.GetName(name),
			db.GetName(parent),
			unique_id);

		db.AddPrimitive(primitive);
	}


	void ParseClass(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Type parsing
		cldb::u32 size = tok.GetHexInt();

		// Class parsing
		cldb::u32 base = tok.GetHexInt();

		// Add a new class to the database
		cldb::Class primitive(
			db.GetName(name),
			db.GetName(parent),
			db.GetName(base),
			size);

		db.AddPrimitive(primitive);
	}


	void ParseTemplateType(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Template type argument parsing
		cldb::TemplateType primitive(db.GetName(name), db.GetName(parent));
		for (int i = 0; i < cldb::TemplateType::MAX_NB_ARGS; i++)
		{
			cldb::u32 type = tok.GetHexInt();
			if (type == 0)
			{
				break;
			}

			primitive.parameter_types[i] = db.GetName(type);
			primitive.parameter_ptrs[i] = atoi(tok.Get()) != 0;
		}

		db.AddPrimitive(primitive);
	}


	void ParseIntAttribute(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Int attribute parsing
		int value = atoi(tok.Get());

		// Add a new attribute to the database
		cldb::IntAttribute primitive(
			db.GetName(name),
			db.GetName(parent),
			value);

		db.AddPrimitive(primitive);
	}


	void ParseFloatAttribute(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Attribute parsing
		float value = 0;
		sscanf(tok.Get(), "%f", &value);

		// Add a new attribute to the database
		cldb::FloatAttribute primitive(
			db.GetName(name),
			db.GetName(parent),
			value);

		db.AddPrimitive(primitive);
	}


	void ParseNameAttribute(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Attribute parsing
		cldb::u32 value = tok.GetHexInt();

		// Add a new attribute to the database
		cldb::NameAttribute primitive(
			db.GetName(name),
			db.GetName(parent),
			db.GetName(value));

		db.AddPrimitive(primitive);
	}


	void ParseTextAttribute(char* line, cldb::Database& db)
	{
		StringTokeniser tok(line, "\t");

		// Primitive parsing
		cldb::u32 name, parent;
		tok.GetNameAndParent(name, parent);

		// Attribute parsing
		const char* value = tok.Get();

		// Add a new attribute to the database
		cldb::TextAttribute primitive(
			db.GetName(name),
			db.GetName(parent),
			value);

		db.AddPrimitive(primitive);
	}


	template <typename PARSE_FUNC>
	void ParseTable(FILE* fp, char* line, cldb::Database& db, const char* table_name, PARSE_FUNC parse_func)
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


bool cldb::ReadTextDatabase(const char* filename, Database& db)
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
		ParseTable(fp, line, db, "Namespaces", ParsePrimitive<cldb::Namespace>);
		ParseTable(fp, line, db, "Types", ParseType);
		ParseTable(fp, line, db, "Enum Constants", ParseEnumConstant);
		ParseTable(fp, line, db, "Enums", ParseEnum);
		ParseTable(fp, line, db, "Fields", ParseField);
		ParseTable(fp, line, db, "Functions", ParseFunction);
		ParseTable(fp, line, db, "Templates", ParsePrimitive<cldb::Template>);
		ParseTable(fp, line, db, "Template Types", ParseTemplateType);
		ParseTable(fp, line, db, "Classes", ParseClass);
		ParseTable(fp, line, db, "Flag Attributes", ParsePrimitive<cldb::FlagAttribute>);
		ParseTable(fp, line, db, "Int Attributes", ParseIntAttribute);
		ParseTable(fp, line, db, "Float Attributes", ParseFloatAttribute);
		ParseTable(fp, line, db, "Name Attributes", ParseNameAttribute);
		ParseTable(fp, line, db, "Text Attributes", ParseTextAttribute);
	}

	fclose(fp);
	return true;
}


bool cldb::IsTextDatabase(const char* filename)
{
	// Not a database if it doesn't exist
	FILE* fp = fopen(filename, "r");
	if (fp == 0)
		return false;

	// Parse the first few lines looking for the header
	int line_index = 0;
	bool is_text_db = true;
	while (char* line = ReadLine(fp))
	{
		if (startswith(line, "clReflect Database"))
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