
#include "CodeGen.h"

#include <clReflectCore/Database.h>
#include <clReflectCore/FileUtils.h>
#include <clReflectCore/Logging.h>

#include <stdarg.h>
#include <assert.h>
#include <map>


CodeGen::CodeGen()
	: m_Indent(0)
{
}


void CodeGen::Line(const char* format, ...)
{
	// Format to local buffer
	char buffer[512];
	va_list args;
	va_start(args, format);
#ifdef _MSC_VER
	vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
#else
    vsnprintf(buffer, sizeof(buffer), format, args);
#endif  // _MSC_VER
	va_end(args);

	for (int i = 0; i < m_Indent; i++)
		m_Text += "\t";

	m_Text += buffer;
	m_Text += "\r\n";
}


void CodeGen::Line()
{
	// Shortcut for empty line
	Line("");
}


void CodeGen::PrefixLine(const char* format, ...)
{
	// Format to local buffer
	char buffer[512];
	va_list args;
	va_start(args, format);
#ifdef _MSC_VER
	vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
#else
    vsnprintf(buffer, sizeof(buffer), format, args);
#endif  // _MSC_VER
	va_end(args);

	m_Text = buffer + ("\r\n" + m_Text);
}


void CodeGen::Indent()
{
	m_Indent++;
}


void CodeGen::UnIndent()
{
	assert(m_Indent > 0);
	m_Indent--;
}


void CodeGen::EnterScope()
{
	Line("{");
	Indent();
}


void CodeGen::ExitScope()
{
	UnIndent();
	Line("}");
}


void CodeGen::PushUndoPoint()
{
	m_UndoPoints.push_back(m_Text.size());
}


void CodeGen::PopUndoPoint()
{
	m_UndoPoints.pop_back();
}


void CodeGen::Undo()
{
	std::string::size_type undo_point = m_UndoPoints.back();
	m_UndoPoints.pop_back();
	m_Text = m_Text.substr(0, undo_point);
}


unsigned int CodeGen::GenerateHash() const
{
	return clcpp::internal::HashNameString(m_Text.c_str());
}


void CodeGen::WriteToFile(const char* filename)
{
	FILE* fp = fopen(filename, "wb");
	if (fp != 0)
	{
		fwrite(m_Text.c_str(), 1, m_Text.size(), fp);
		fclose(fp);
	}
}


namespace
{
	std::string UnscopeName(const char* name)
	{
		// Reverse search for the scope operator and remove everything before it
		std::string str = name;
		std::string::size_type si = str.rfind("::");
		if (si != std::string::npos)
			str = str.substr(si + 2);
		return str;
	}


	//
	// A set of hierarchical types required to build forward declarations
	// If the source database was hierarhical in the first place, this (and more code)
	// wouldn't be necessary. Work for the future...
	//
	enum PrimType
	{
		PT_Type,
		PT_Class,
		PT_Struct,
		PT_Enum
	};
	struct Primitive
	{
		const char* name;
		unsigned int hash;
		PrimType type;
	};
	struct Namespace
	{
		typedef std::map<unsigned int, Namespace> Map;

		const char* name;
		unsigned int parent;

		// Nested primitives
		std::vector<const Namespace*> namespaces;
		std::vector<Primitive> primitives;
	};


	void BuildNamespaces(const cldb::Database& db, Namespace::Map& namespaces)
	{
		// Straight copy of all namespaces and the information required
		for (cldb::DBMap<cldb::Namespace>::const_iterator i = db.m_Namespaces.begin(); i != db.m_Namespaces.end(); ++i)
		{
			const cldb::Namespace& db_ns = i->second;
			Namespace ns;
			ns.name = db_ns.name.text.c_str();
			ns.parent = db_ns.parent.hash;
			namespaces[db_ns.name.hash] = ns;
		}

		// Add all namespaces to their parent namespaces
		for (Namespace::Map::const_iterator i = namespaces.begin(); i != namespaces.end(); ++i)
		{
			const Namespace& ns = i->second;
			Namespace::Map::iterator j = namespaces.find(ns.parent);
			if (j != namespaces.end())
				j->second.namespaces.push_back(&ns);
		}

		// Build the global namespace
		Namespace global_ns;
		for (Namespace::Map::const_iterator i = namespaces.begin(); i != namespaces.end(); ++i)
		{
			const Namespace& ns = i->second;
			if (ns.parent == 0)
				global_ns.namespaces.push_back(&ns);
		}
		namespaces[0] = global_ns;
	}


	void BuildNamespaceContents(const cldb::Database& db, Namespace::Map& namespaces, std::vector<Primitive>& primitives)
	{
		// Add all classes to their parent namespaces
		// NOTE: This (by design) won't add nested types as they can't be forward declared
		for (cldb::DBMap<cldb::Class>::const_iterator i = db.m_Classes.begin(); i != db.m_Classes.end(); ++i)
		{
			const cldb::Class& db_cls = i->second;
			Namespace::Map::iterator j = namespaces.find(db_cls.parent.hash);
			if (j != namespaces.end())
			{
				Primitive prim;
				prim.name = db_cls.name.text.c_str();
				prim.hash = db_cls.name.hash;
				prim.type = db_cls.is_class ? PT_Class : PT_Struct;
				j->second.primitives.push_back(prim);
				primitives.push_back(prim);
			}
		}

		// Add all enums to their parent namespaces
		// NOTE: This (by design) won't add nested enums as they can't be forward declared
		for (cldb::DBMap<cldb::Enum>::const_iterator i = db.m_Enums.begin(); i != db.m_Enums.end(); ++i)
		{
			const cldb::Enum& db_en = i->second;
			Namespace::Map::iterator j = namespaces.find(db_en.parent.hash);
			if (j != namespaces.end())
			{
				Primitive prim;
				prim.name = db_en.name.text.c_str();
				prim.hash = db_en.name.hash;
				prim.type = PT_Enum;
				j->second.primitives.push_back(prim);
				primitives.push_back(prim);
			}
		}

		// Put all native types in the global namespace
		Namespace& global_ns = namespaces[0];
		for (cldb::DBMap<cldb::Type>::const_iterator i = db.m_Types.begin(); i != db.m_Types.end(); ++i)
		{
			const cldb::Type& db_type = i->second;
			Primitive prim;
			prim.name = db_type.name.text.c_str();
			prim.hash = db_type.name.hash;
			prim.type = PT_Type;
			global_ns.primitives.push_back(prim);
			primitives.push_back(prim);
		}		
	}


	void GenNamespaceForwardDeclare(CodeGen& cg, const Namespace* ns, bool root)
	{
		// Any undos will remove everything done in this entire function and below
		cg.PushUndoPoint();

		if (!root)
		{
			std::string name = UnscopeName(ns->name);
			cg.Line("namespace %s", name.c_str());
			cg.EnterScope();
		}

		// Start point and end point are within the parent namespace scope to allow
		// detection of no output for arbitrarily complex code generation.
		std::string::size_type start_point = cg.Size();

		// Forward declare nested namespaces
		for (size_t i = 0; i < ns->namespaces.size(); i++)
			GenNamespaceForwardDeclare(cg, ns->namespaces[i], false);

		// Forward declare primitives
		for (size_t i = 0; i < ns->primitives.size(); i++)
		{
			const Primitive& prim = ns->primitives[i];
			std::string name_str = UnscopeName(prim.name);
			if (prim.type == PT_Class)
				cg.Line("class %s;", name_str.c_str());
			else if (prim.type == PT_Struct)
				cg.Line("struct %s;", name_str.c_str());
			else if (prim.type == PT_Enum)
				cg.Line("enum %s;", name_str.c_str());
		}

		std::string::size_type end_point = cg.Size();

		// Emit the exit scope before undo so that the indentation is restored
		if (!root)
			cg.ExitScope();

		// Either undo or discard the undo point
		if (start_point == end_point)
			cg.Undo();
		else
			cg.PopUndoPoint();
	}
}


void GenMergedCppImpl(const char* filename, const cldb::Database& db)
{
	// Build a light-weight, hierarchical representation of the incoming database
	Namespace::Map namespaces;
	std::vector<Primitive> primitives;
	BuildNamespaces(db, namespaces);
	BuildNamespaceContents(db, namespaces, primitives);

	// Include clcpp headers
	CodeGen cg;
	cg.Line("// Generated by clmerge.exe - do not edit!");
	cg.Line("#include <clcpp/clcpp.h>");
	cg.Line();

	// Generate arrays
	cg.Line("// Array of type name hases and pointers");
	cg.Line("static const int clcppNbTypes = %d;", primitives.size());
	cg.Line("static unsigned int clcppTypeNameHashes[clcppNbTypes] = { 0 };");
	cg.Line("static const clcpp::Type* clcppTypePtrs[clcppNbTypes] = { 0 };");
	cg.Line();

	// Generate initialisation function
	cg.Line("void clcppInitGetType(const clcpp::Database* db)");
	cg.EnterScope();
	cg.Line("// Specify hashes for all known types");
	for (size_t i = 0; i < primitives.size(); i++)
		cg.Line("clcppTypeNameHashes[%d] = 0x%x;", i, primitives[i].hash);
	cg.Line();
	cg.Line("// Populate the type pointer array if a database is specified");
	cg.Line("if (db != 0)");
	cg.EnterScope();
	cg.Line("for (int i = 0; i < clcppNbTypes; i++)");
	cg.Line("\tclcppTypePtrs[i] = db->GetType(clcppTypeNameHashes[i]);");
	cg.ExitScope();
	cg.ExitScope();
	cg.Line();

	// Generate forward declarations
	cg.Line("// Forward declarations for all known types");
	GenNamespaceForwardDeclare(cg, &namespaces[0], true);
	cg.Line();

	// Generate the implementations
	cg.Line("// Specialisations for GetType and GetTypeNameHash");
	cg.Line("namespace clcpp");
	cg.EnterScope();
	for (size_t i = 0; i < primitives.size(); i++)
	{
		const char* name = primitives[i].name;
		cg.Line("template <> const Type* GetType<%s>() { return clcppTypePtrs[%d]; }", name, i);
		cg.Line("template <> unsigned int GetTypeNameHash<%s>() { return clcppTypeNameHashes[%d]; }", name, i);
	}
	cg.ExitScope();

	// Generate the hash for the generated code so far
	unsigned int hash = cg.GenerateHash();
	cg.PrefixLine("// %x", hash);

	// If the output file already exists, open it and read its hash
	unsigned int existing_hash = hash + 1;
	FILE* fp = fopen(filename, "rb");
	if (fp != 0)
	{
		fscanf(fp, "// %x", &existing_hash);
		fclose(fp);
	}

	// Only write if there are changes
	if (existing_hash != hash)
	{
		LOG(main, INFO, "Generating C++ file: %s\n", filename);
		cg.WriteToFile(filename);
	}
}