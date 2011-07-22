
#include "CppExport.h"

#include <ClangReflectCore\Database.h>
#include <ClangReflectCore\Logging.h>

#include <crcpp\Database.h>


namespace
{
	// Overloads for copying between databases
	void CopyPrimitive(crcpp::Primitive& dest, const crdb::Primitive& src)
	{
	}
	void CopyPrimitive(crcpp::EnumConstant& dest, const crdb::EnumConstant& src)
	{
		dest.value = src.value;
	}
	void CopyPrimitive(crcpp::Field& dest, const crdb::Field& src)
	{
		dest.type = (crcpp::Type*)src.type.hash;
		dest.is_const = src.is_const;
		dest.offset = src.offset;
		switch (src.modifier)
		{
		case (crdb::Field::MODIFIER_VALUE): dest.modifier = crcpp::Field::MODIFIER_VALUE; break;
		case (crdb::Field::MODIFIER_POINTER): dest.modifier = crcpp::Field::MODIFIER_POINTER; break;
		case (crdb::Field::MODIFIER_REFERENCE): dest.modifier = crcpp::Field::MODIFIER_REFERENCE; break;
		default: assert(false && "Case not handled");
		}
	}
	void CopyPrimitive(crcpp::Class& dest, const crdb::Class& src)
	{
		dest.base_class = (crcpp::Class*)src.base_class.hash;
		dest.size = src.size;
	}


	template <typename CRDB_TYPE, typename CRCPP_TYPE>
	void BuildCArray(CppExport& cppexp, crcpp::CArray<CRCPP_TYPE>& dest, const crdb::Database& db, bool named = true)
	{
		// Allocate enough entries for all primitives
		const crdb::PrimitiveStore<CRDB_TYPE>& src = db.GetPrimitiveStore<CRDB_TYPE>();
		dest = crcpp::CArray<CRCPP_TYPE>(src.size());

		// Copy individually
		int index = 0;
		for (crdb::PrimitiveStore<CRDB_TYPE>::const_iterator i = src.begin(); i != src.end(); ++i)
		{
			// Copy as a primitive first
			CRCPP_TYPE& dest_prim = dest[index++];
			const CRDB_TYPE& src_prim = i->second;
			dest_prim.kind = CRCPP_TYPE::KIND;
			dest_prim.name.hash = src_prim.name.hash;
			dest_prim.parent = (crcpp::Primitive*)src_prim.parent.hash;

			// Early reference the text of the name for easier debugging
			int offset = cppexp.name_hash_map[src_prim.name.hash];
			dest_prim.name.text = &cppexp.name_data[offset];

			// Then custom
			CopyPrimitive(dest_prim, src_prim);
		}
	}


	template <typename PARENT_TYPE, typename CHILD_TYPE>
	void Parent(crcpp::CArray<PARENT_TYPE>& parents, crcpp::CArray<const CHILD_TYPE*> (PARENT_TYPE::*carray), crcpp::CArray<CHILD_TYPE>& children)
	{
		// Create a lookup table from hash ID to parent and the number of references the parent has
		typedef std::pair<PARENT_TYPE*, int> ParentAndRefCount;
		typedef std::map<unsigned int, ParentAndRefCount> ParentMap;
		ParentMap parent_map;
		for (int i = 0; i < parents.size(); i++)
		{
			PARENT_TYPE& parent = parents[i];
			parent_map[parent.name.hash] = ParentAndRefCount(&parent, 0);
		}

		// Assign parents and count the references
		for (int i = 0; i < children.size(); i++)
		{
			CHILD_TYPE& child = children[i];
			ParentMap::iterator j = parent_map.find((unsigned int)child.parent);
			if (j != parent_map.end())
			{
				ParentAndRefCount& parc = j->second;
				child.parent = parc.first;
				parc.second++;
			}
		}

		// Allocate the arrays in the parent
		for (ParentMap::iterator i = parent_map.begin(); i != parent_map.end(); ++i)
		{
			if (int nb_refs = i->second.second)
			{
				PARENT_TYPE& parent = *i->second.first;
				(parent.*carray) = crcpp::CArray<const CHILD_TYPE*>(nb_refs);

				// To save having to do any further lookups, store the count inside the array
				// at the end
				(parent.*carray)[nb_refs - 1] = 0;
			}
		}

		// Fill in all the arrays
		for (int i = 0; i < children.size(); i++)
		{
			CHILD_TYPE& child = children[i];
			PARENT_TYPE* parent = (PARENT_TYPE*)child.parent;

			// Only process if the parent has been correctly assigned
			if (parent >= parents.data() && parent < parents.data() + parents.size())
			{
				// Locate the current constant count at the end of the array and add this constant
				// to its parent
				int nb_constants = (parent->*carray).size();
				int cur_count = (int)(parent->*carray)[nb_constants - 1];
				(parent->*carray)[cur_count++] = &child;

				// When the last constant gets written, the constant count gets overwritten with
				// the constant pointer and should no longer be updated
				if (cur_count != nb_constants)
				{
					(parent->*carray)[nb_constants - 1] = (CHILD_TYPE*)cur_count;
				}
			}
		}
	}


	template <typename PARENT_TYPE, typename FIELD_TYPE, typename CHILD_TYPE>
	void Link(crcpp::CArray<PARENT_TYPE>& parents, const FIELD_TYPE* (PARENT_TYPE::*field), crcpp::CArray<CHILD_TYPE>& children)
	{
		// Create a lookup table from hash ID to child
		typedef std::map<unsigned int, CHILD_TYPE*> ChildMap;
		ChildMap child_map;
		for (int i = 0; i < children.size(); i++)
		{
			CHILD_TYPE& child = children[i];
			child_map[child.name.hash] = &child;
		}

		// Link up the pointers
		for (int i = 0; i < parents.size(); i++)
		{
			PARENT_TYPE& parent = parents[i];
			unsigned int hash_id = (unsigned int)(parent.*field);
			ChildMap::iterator j = child_map.find(hash_id);
			if (j != child_map.end())
			{
				(parent.*field) = j->second;
			}
		}
	}
}


void BuildCppExport(const crdb::Database& db, CppExport& cppexp)
{
	for (crdb::NameMap::const_iterator i = db.m_Names.begin(); i != db.m_Names.end(); ++i)
	{
		cppexp.name_hash_map[i->first] = (int)cppexp.name_data.size();
		const crdb::Name& name = i->second;
		cppexp.name_data.insert(cppexp.name_data.end(), name.text.begin(), name.text.end());
		cppexp.name_data.push_back(0);
	}

	BuildCArray<crdb::Type>(cppexp, cppexp.types, db);
	BuildCArray<crdb::Class>(cppexp, cppexp.classes, db);
	BuildCArray<crdb::Enum>(cppexp, cppexp.enums, db);
	BuildCArray<crdb::EnumConstant>(cppexp, cppexp.enum_constants, db);
	BuildCArray<crdb::Function>(cppexp, cppexp.functions, db);
	BuildCArray<crdb::Field>(cppexp, cppexp.fields, db);
	BuildCArray<crdb::Field>(cppexp, cppexp.unnamed_fields, db, false);
	BuildCArray<crdb::Namespace>(cppexp, cppexp.namespaces, db);

	Parent(cppexp.enums, &crcpp::Enum::constants, cppexp.enum_constants);
	//Parent(functions, &crcpp::Function::parameters, fields);
	//Parent(functions, &crcpp::Function::parameters, unnamed_fields);
	Parent(cppexp.classes, &crcpp::Class::enums, cppexp.enums);
	Parent(cppexp.classes, &crcpp::Class::classes, cppexp.classes);
	Parent(cppexp.classes, &crcpp::Class::methods, cppexp.functions);
	Parent(cppexp.classes, &crcpp::Class::fields, cppexp.fields);
	Parent(cppexp.namespaces, &crcpp::Namespace::namespaces, cppexp.namespaces);
	Parent(cppexp.namespaces, &crcpp::Namespace::types, cppexp.types);
	Parent(cppexp.namespaces, &crcpp::Namespace::enums, cppexp.enums);
	Parent(cppexp.namespaces, &crcpp::Namespace::classes, cppexp.classes);
	Parent(cppexp.namespaces, &crcpp::Namespace::functions, cppexp.functions);

	Link(cppexp.fields, &crcpp::Field::type, cppexp.types);
	Link(cppexp.fields, &crcpp::Field::type, cppexp.enums);
	Link(cppexp.fields, &crcpp::Field::type, cppexp.classes);
	Link(cppexp.classes, &crcpp::Class::base_class, cppexp.classes);
}


template <typename TYPE>
void LogPrimitives(const crcpp::CArray<const TYPE*>& primitives)
{
	for (int i = 0; i < primitives.size(); i++)
	{
		LogPrimitive(*primitives[i]);
	}
}


template <typename TYPE>
void LogGlobalPrimitives(const crcpp::CArray<TYPE>& primitives)
{
	for (int i = 0; i < primitives.size(); i++)
	{
		if (primitives[i].parent == 0)
		{
			LogPrimitive(primitives[i]);
			LOG_NEWLINE(cppexp);
		}
	}
}


void LogPrimitive(const crcpp::Function& function)
{
	LOG(cppexp, INFO, "%s\n", function.name.text);
}


void LogPrimitive(const crcpp::EnumConstant& constant)
{
	LOG(cppexp, INFO, "%s = %d,\n", constant.name.text, constant.value);
}

void LogPrimitive(const crcpp::Enum& e)
{
	LOG(cppexp, INFO, "enum %s\n", e.name.text);
	LOG(cppexp, INFO, "{\n");
	LOG_PUSH_INDENT(cppexp);

	LogPrimitives(e.constants);

	LOG_POP_INDENT(cppexp);
	LOG(cppexp, INFO, "};\n");
}


void LogPrimitive(const crcpp::Field& field)
{
	LOG(cppexp, INFO, "%s", field.is_const ? "const " : "");
	LOG_APPEND(cppexp, INFO, "%s", field.type->name.text);
	LOG_APPEND(cppexp, INFO, "%s", field.modifier == crcpp::Field::MODIFIER_POINTER ? "*" :
								   field.modifier == crcpp::Field::MODIFIER_REFERENCE ? "&" : "");
	LOG_APPEND(cppexp, INFO, " %s\n", field.name.text);
}


void LogPrimitive(const crcpp::Class& cls)
{
	LOG(cppexp, INFO, "class %s", cls.name.text);
	if (cls.base_class)
	{
		LOG_APPEND(cppexp, INFO, " : public %s\n", cls.base_class->name.text);
	}
	else
	{
		LOG_APPEND(cppexp, INFO, "\n");
	}
	LOG(cppexp, INFO, "{\n");
	LOG_PUSH_INDENT(cppexp);

	LogPrimitives(cls.classes);
	LogPrimitives(cls.fields);
	LogPrimitives(cls.enums);
	LogPrimitives(cls.methods);

	LOG_POP_INDENT(cppexp);
	LOG(cppexp, INFO, "};\n");
}


void LogPrimitive(const crcpp::Namespace& ns)
{
	LOG(cppexp, INFO, "namespace %s\n", ns.name.text);
	LOG(cppexp, INFO, "{\n");
	LOG_PUSH_INDENT(cppexp);

	LogPrimitives(ns.namespaces);
	LogPrimitives(ns.classes);
	LogPrimitives(ns.enums);
	LogPrimitives(ns.functions);

	LOG_POP_INDENT(cppexp);
	LOG(cppexp, INFO, "}\n");
}


void WriteCppExportAsText(const CppExport& cppexp, const char* filename)
{
	LOG_TO_FILE(cppexp, ALL, filename);

	LogGlobalPrimitives(cppexp.namespaces);
	LogGlobalPrimitives(cppexp.classes);
	LogGlobalPrimitives(cppexp.enums);
	LogGlobalPrimitives(cppexp.functions);
}