
// TODO:
//	* Save to disk
//	* Gather type size
//	* Check that every pointer has been linked up

#include "CppExport.h"

#include <ClangReflectCore\Database.h>
#include <ClangReflectCore\Logging.h>

#include <crcpp\Database.h>

#include <algorithm>


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
	void CopyPrimitive(crcpp::Function& dest, const crdb::Function& src)
	{
		dest.unique_id = src.unique_id;
	}
	void CopyPrimitive(crcpp::Field& dest, const crdb::Field& src)
	{
		dest.type = (crcpp::Type*)src.type.hash;
		dest.is_const = src.is_const;
		dest.offset = src.offset;
		dest.parent_unique_id = src.parent_unique_id;
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
	void BuildCArray(CppExport& cppexp, crcpp::CArray<CRCPP_TYPE>& dest, const crdb::Database& db)
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


	bool ParentAndChildMatch(const crcpp::Primitive&, const crcpp::Primitive&)
	{
		return true;
	}
	bool ParentAndChildMatch(const crcpp::Function& parent, const crcpp::Field& child)
	{
		return parent.unique_id == child.parent_unique_id;
	}


	template <typename PARENT_TYPE, typename CHILD_TYPE>
	void Parent(crcpp::CArray<PARENT_TYPE>& parents, crcpp::CArray<const CHILD_TYPE*> (PARENT_TYPE::*carray), crcpp::CArray<CHILD_TYPE>& children)
	{
		// Create a lookup table from hash ID to parent and the number of references the parent has
		typedef std::pair<PARENT_TYPE*, int> ParentAndRefCount;
		typedef std::multimap<unsigned int, ParentAndRefCount> ParentMap;
		typedef std::pair<ParentMap::iterator, ParentMap::iterator> ParentMapRange;
		ParentMap parent_map;
		for (int i = 0; i < parents.size(); i++)
		{
			PARENT_TYPE& parent = parents[i];
			parent_map.insert(ParentMap::value_type(parent.name.hash, ParentAndRefCount(&parent, 0)));
		}

		// Assign parents and count the references
		for (int i = 0; i < children.size(); i++)
		{
			CHILD_TYPE& child = children[i];

			// Iterate over all matches
			ParentMapRange range = parent_map.equal_range((unsigned int)child.parent);
			for (ParentMap::iterator j = range.first; j != range.second; ++j)
			{
				ParentAndRefCount& parc = j->second;
				if (ParentAndChildMatch(*parc.first, child))
				{
					child.parent = parc.first;
					parc.second++;
					break;
				}
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
		typedef std::multimap<unsigned int, CHILD_TYPE*> ChildMap;
		typedef std::pair<ChildMap::iterator, ChildMap::iterator> ChildMapRange;
		ChildMap child_map;
		for (int i = 0; i < children.size(); i++)
		{
			CHILD_TYPE& child = children[i];
			child_map.insert(ChildMap::value_type(child.name.hash, &child));
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


	int ReturnParameterIndex(const crcpp::CArray<const crcpp::Field*>& parameters)
	{
		// Linear search for the named return value
		static unsigned int return_hash = crdb::HashNameString("return");
		for (int i = 0; i < parameters.size(); i++)
		{
			if (parameters[i]->name.hash == return_hash)
			{
				return i;
			}
		}
		return -1;
	}


	void AssignReturnParameters(CppExport& cppexp)
	{
		// Iterate over every function that has the return parameter in its parameter list
		for (int i = 0; i < cppexp.functions.size(); i++)
		{
			crcpp::Function& func = cppexp.functions[i];
			int return_index = ReturnParameterIndex(func.parameters);
			if (return_index == -1)
			{
				continue;
			}

			// Assign the return parameter and create a new parameter list without it
			// [xyRxyxy](2) -> [xyxyxy]
			// [R](0) -> []
			func.return_parameter = func.parameters[return_index];
			crcpp::CArray<const crcpp::Field*> new_parameters(func.parameters.size() - 1);
			const crcpp::Field** src = func.parameters.data();
			const crcpp::Field** dest = new_parameters.data();
			std::copy(src, src + return_index, dest);
			std::copy(src + return_index + 1, src + func.parameters.size(), dest + return_index);
			func.parameters = new_parameters;
		}
	}


	template <typename TYPE>
	int CountGlobalPrimitives(const crcpp::CArray<TYPE>& primitives)
	{
		// Finding all unparented primitives
		int nb_global_primitives = 0;
		for (int i = 0; i < primitives.size(); i++)
		{
			if (primitives[i].parent == 0)
			{
				nb_global_primitives++;
			}
		}
		return nb_global_primitives;
	}


	template <typename TYPE>
	void GatherGlobalPrimitives(crcpp::CArray<const TYPE*>& dest, const crcpp::CArray<TYPE>& src)
	{
		// Allocate enough space for the primitives
		int nb_global_primitives = CountGlobalPrimitives(src);
		dest = crcpp::CArray<const TYPE*>(nb_global_primitives);

		// Gather all unparented primitives
		int index = 0;
		for (int i = 0; i < src.size(); i++)
		{
			if (src[i].parent == 0)
			{
				dest[index++] = &src[i];
			}
		}
	}


	void BuildGlobalNamespace(CppExport& cppexp)
	{
		GatherGlobalPrimitives(cppexp.global_namespace.namespaces, cppexp.namespaces);
		GatherGlobalPrimitives(cppexp.global_namespace.types, cppexp.types);
		GatherGlobalPrimitives(cppexp.global_namespace.enums, cppexp.enums);
		GatherGlobalPrimitives(cppexp.global_namespace.classes, cppexp.classes);
		GatherGlobalPrimitives(cppexp.global_namespace.functions, cppexp.functions);
	}


	// Sort an array of primitive pointers by name
	bool SortPrimitiveByName(const crcpp::Primitive* a, const crcpp::Primitive* b)
	{
		return a->name.hash < b->name.hash;
	}
	template <typename TYPE>
	void SortPrimitives(crcpp::CArray<const TYPE*>& primitives)
	{
		std::sort(primitives.data(), primitives.data() + primitives.size(), SortPrimitiveByName);
	}


	// Overloads for sorting primitive arrays within a primitive
	void SortPrimitives(crcpp::Enum& primitive)
	{
		SortPrimitives(primitive.constants);
	}
	void SortPrimitives(crcpp::Function& primitive)
	{
		SortPrimitives(primitive.parameters);
	}
	void SortPrimitives(crcpp::Class& primitive)
	{
		SortPrimitives(primitive.enums);
		SortPrimitives(primitive.classes);
		SortPrimitives(primitive.methods);
		SortPrimitives(primitive.fields);
	}
	void SortPrimitives(crcpp::Namespace& primitive)
	{
		SortPrimitives(primitive.namespaces);
		SortPrimitives(primitive.types);
		SortPrimitives(primitive.enums);
		SortPrimitives(primitive.classes);
		SortPrimitives(primitive.functions);
	}


	// Iterate over all provided primitives and sort their primitive arrays
	template <typename TYPE>
	void SortPrimitives(crcpp::CArray<TYPE>& primitives)
	{
		for (int i = 0; i < primitives.size(); i++)
		{
			SortPrimitives(primitives[i]);
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

	// Generate a raw crcpp equivalent of the crdb database. At this point no primitives
	// will physically point to or contain each other, but they will reference each other
	// using hash values aliased in their pointers.
	BuildCArray<crdb::Type>(cppexp, cppexp.types, db);
	BuildCArray<crdb::Class>(cppexp, cppexp.classes, db);
	BuildCArray<crdb::Enum>(cppexp, cppexp.enums, db);
	BuildCArray<crdb::EnumConstant>(cppexp, cppexp.enum_constants, db);
	BuildCArray<crdb::Function>(cppexp, cppexp.functions, db);
	BuildCArray<crdb::Field>(cppexp, cppexp.fields, db);
	BuildCArray<crdb::Namespace>(cppexp, cppexp.namespaces, db);

	// Construct the primitive scope hierarchy, pointing primitives at their parents
	// and adding them to the arrays within their parents.
	Parent(cppexp.enums, &crcpp::Enum::constants, cppexp.enum_constants);
	Parent(cppexp.functions, &crcpp::Function::parameters, cppexp.fields);
	Parent(cppexp.classes, &crcpp::Class::enums, cppexp.enums);
	Parent(cppexp.classes, &crcpp::Class::classes, cppexp.classes);
	Parent(cppexp.classes, &crcpp::Class::methods, cppexp.functions);
	Parent(cppexp.classes, &crcpp::Class::fields, cppexp.fields);
	Parent(cppexp.namespaces, &crcpp::Namespace::namespaces, cppexp.namespaces);
	Parent(cppexp.namespaces, &crcpp::Namespace::types, cppexp.types);
	Parent(cppexp.namespaces, &crcpp::Namespace::enums, cppexp.enums);
	Parent(cppexp.namespaces, &crcpp::Namespace::classes, cppexp.classes);
	Parent(cppexp.namespaces, &crcpp::Namespace::functions, cppexp.functions);

	// Link up any references between primitives
	Link(cppexp.fields, &crcpp::Field::type, cppexp.types);
	Link(cppexp.fields, &crcpp::Field::type, cppexp.enums);
	Link(cppexp.fields, &crcpp::Field::type, cppexp.classes);
	Link(cppexp.classes, &crcpp::Class::base_class, cppexp.classes);

	// Return parameters are parented to their functions as parameters. Move them from
	// wherever they are in the list and into the return parameter data member.
	AssignReturnParameters(cppexp);

	// Now gather any unparented primitives into the root namespace
	BuildGlobalNamespace(cppexp);

	// Sort any primitive pointer arrays in the database by name hash, ascending. This
	// is to allow fast O(logN) searching of the primitive arrays at runtime with a
	// binary search.
	SortPrimitives(cppexp.enums);
	SortPrimitives(cppexp.functions);
	SortPrimitives(cppexp.classes);
	SortPrimitives(cppexp.namespaces);
}


namespace
{
	template <typename TYPE>
	void LogPrimitives(const crcpp::CArray<const TYPE*>& primitives)
	{
		for (int i = 0; i < primitives.size(); i++)
		{
			LogPrimitive(*primitives[i]);
			LOG_NEWLINE(cppexp);
		}
	}


	void LogField(const crcpp::Field& field, bool name = true)
	{
		LOG_APPEND(cppexp, INFO, "%s", field.is_const ? "const " : "");
		LOG_APPEND(cppexp, INFO, "%s", field.type->name.text);
		LOG_APPEND(cppexp, INFO, "%s", field.modifier == crcpp::Field::MODIFIER_POINTER ? "*" :
			field.modifier == crcpp::Field::MODIFIER_REFERENCE ? "&" : "");

		if (name)
		{
			LOG_APPEND(cppexp, INFO, " %s", field.name.text);
		}
	}


	void LogPrimitive(const crcpp::Field& field)
	{
		LOG(cppexp, INFO, "");
		LogField(field);
		LOG_APPEND(cppexp, INFO, ";");
	}


	void LogPrimitive(const crcpp::Function& func)
	{
		if (func.return_parameter)
		{
			LOG(cppexp, INFO, "");
			LogField(*func.return_parameter, false);
		}
		else
		{
			LOG(cppexp, INFO, "void");
		}

		LOG_APPEND(cppexp, INFO, " %s(", func.name.text);

		for (int i = 0; i < func.parameters.size(); i++)
		{
			LogField(*func.parameters[i]);
			if (i != func.parameters.size() - 1)
			{
				LOG_APPEND(cppexp, INFO, ", ");
			}
		}

		LOG_APPEND(cppexp, INFO, ");");
	}


	void LogPrimitive(const crcpp::EnumConstant& constant)
	{
		LOG(cppexp, INFO, "%s = %d,", constant.name.text, constant.value);
	}


	void LogPrimitive(const crcpp::Enum& e)
	{
		LOG(cppexp, INFO, "enum %s\n", e.name.text);
		LOG(cppexp, INFO, "{\n");
		LOG_PUSH_INDENT(cppexp);

		LogPrimitives(e.constants);

		LOG_POP_INDENT(cppexp);
		LOG(cppexp, INFO, "};");
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
		LOG(cppexp, INFO, "};");
	}


	void LogPrimitive(const crcpp::Namespace& ns)
	{
		if (ns.name.text)
		{
			LOG(cppexp, INFO, "namespace %s\n", ns.name.text);
			LOG(cppexp, INFO, "{\n");
			LOG_PUSH_INDENT(cppexp);
		}

		LogPrimitives(ns.namespaces);
		LogPrimitives(ns.classes);
		LogPrimitives(ns.enums);
		LogPrimitives(ns.functions);

		if (ns.name.text)
		{
			LOG_POP_INDENT(cppexp);
			LOG(cppexp, INFO, "}");
		}
	}
}


void WriteCppExportAsText(const CppExport& cppexp, const char* filename)
{
	LOG_TO_FILE(cppexp, ALL, filename);
	LogPrimitive(cppexp.global_namespace);
}