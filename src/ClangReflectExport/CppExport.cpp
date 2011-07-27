
// TODO:
//	* Save to disk
//	* Check that every pointer has been linked up

#include "CppExport.h"

#include <ClangReflectCore\Database.h>
#include <ClangReflectCore\Logging.h>

#include <crcpp\Database.h>

#include <algorithm>


namespace
{
	// Overloads for copying between databases
	// TODO: Rewrite with database metadata?
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
	void CopyPrimitive(crcpp::Type& dest, const crdb::Type& src)
	{
		dest.size = src.size;
	}
	void CopyPrimitive(crcpp::Class& dest, const crdb::Class& src)
	{
		dest.size = src.size;
		dest.base_class = (crcpp::Class*)src.base_class.hash;
	}


	template <typename CRDB_TYPE, typename CRCPP_TYPE>
	void BuildCArray(CppExport& cppexp, crcpp::CArray<CRCPP_TYPE>& dest, const crdb::Database& db)
	{
		// Allocate enough entries for all primitives
		const crdb::PrimitiveStore<CRDB_TYPE>& src = db.GetPrimitiveStore<CRDB_TYPE>();
		dest = crcpp::CArray<CRCPP_TYPE>(cppexp.allocator.Alloc<CRCPP_TYPE>(src.size()), src.size());

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
			dest_prim.name.text = cppexp.name_map[src_prim.name.hash];

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
	void Parent(crcpp::CArray<PARENT_TYPE>& parents, crcpp::CArray<const CHILD_TYPE*> (PARENT_TYPE::*carray), crcpp::CArray<CHILD_TYPE>& children, StackAllocator& allocator)
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
				(parent.*carray) = crcpp::CArray<const CHILD_TYPE*>(allocator.Alloc<const CHILD_TYPE*>(nb_refs), nb_refs);

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
		for (int i = 0; i < cppexp.db->functions.size(); i++)
		{
			crcpp::Function& func = cppexp.db->functions[i];
			int return_index = ReturnParameterIndex(func.parameters);
			if (return_index == -1)
			{
				continue;
			}

			// Assign the return parameter and remove it from the parameter list
			func.return_parameter = func.parameters[return_index];
			func.parameters.unstable_remove(return_index);
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
	void GatherGlobalPrimitives(crcpp::CArray<const TYPE*>& dest, const crcpp::CArray<TYPE>& src, StackAllocator& allocator)
	{
		// Allocate enough space for the primitives
		int nb_global_primitives = CountGlobalPrimitives(src);
		dest = crcpp::CArray<const TYPE*>(allocator.Alloc<const TYPE*>(nb_global_primitives), nb_global_primitives);

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
		GatherGlobalPrimitives(cppexp.db->global_namespace.namespaces, cppexp.db->namespaces, cppexp.allocator);
		GatherGlobalPrimitives(cppexp.db->global_namespace.types, cppexp.db->types, cppexp.allocator);
		GatherGlobalPrimitives(cppexp.db->global_namespace.enums, cppexp.db->enums, cppexp.allocator);
		GatherGlobalPrimitives(cppexp.db->global_namespace.classes, cppexp.db->classes, cppexp.allocator);
		GatherGlobalPrimitives(cppexp.db->global_namespace.functions, cppexp.db->functions, cppexp.allocator);
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


	void BuildNames(const crdb::Database& db, CppExport& cppexp)
	{
		// Allocate the name data
		unsigned int name_data_size = 0;
		for (crdb::NameMap::const_iterator i = db.m_Names.begin(); i != db.m_Names.end(); ++i)
		{
			name_data_size += i->second.text.length() + 1;
		}
		cppexp.db->name_text_data = cppexp.allocator.Alloc<char>(name_data_size);

		// Populate the name data and build the sorted name map
		name_data_size = 0;
		for (crdb::NameMap::const_iterator i = db.m_Names.begin(); i != db.m_Names.end(); ++i)
		{
			char* text_ptr = (char*)(cppexp.db->name_text_data + name_data_size);
			cppexp.name_map[i->first] = text_ptr;
			const crdb::Name& name = i->second;
			strcpy(text_ptr, name.text.c_str());
			name_data_size += name.text.length() + 1;
		}

		// Build the in-memory name array
		unsigned int nb_names = cppexp.name_map.size();
		cppexp.db->names = crcpp::CArray<crcpp::Name>(cppexp.allocator.Alloc<crcpp::Name>(nb_names), nb_names);
		unsigned int index = 0;
		for (CppExport::NameMap::const_iterator i = cppexp.name_map.begin(); i != cppexp.name_map.end(); ++i)
		{
			crcpp::Name name;
			name.hash = i->first;
			name.text = i->second;
			cppexp.db->names[index++] = name;
		}
	}
}


void BuildCppExport(const crdb::Database& db, CppExport& cppexp)
{
	// Allocate the in-memory database
	cppexp.db = cppexp.allocator.Alloc<crcpp::DatabaseMem>(1);

	// Build all the name data ready for the client to use and the exporter to debug with
	BuildNames(db, cppexp);

	// Generate a raw crcpp equivalent of the crdb database. At this point no primitives
	// will physically point to or contain each other, but they will reference each other
	// using hash values aliased in their pointers.
	BuildCArray<crdb::Type>(cppexp, cppexp.db->types, db);
	BuildCArray<crdb::Class>(cppexp, cppexp.db->classes, db);
	BuildCArray<crdb::Enum>(cppexp, cppexp.db->enums, db);
	BuildCArray<crdb::EnumConstant>(cppexp, cppexp.db->enum_constants, db);
	BuildCArray<crdb::Function>(cppexp, cppexp.db->functions, db);
	BuildCArray<crdb::Field>(cppexp, cppexp.db->fields, db);
	BuildCArray<crdb::Namespace>(cppexp, cppexp.db->namespaces, db);

	// Construct the primitive scope hierarchy, pointing primitives at their parents
	// and adding them to the arrays within their parents.
	Parent(cppexp.db->enums, &crcpp::Enum::constants, cppexp.db->enum_constants, cppexp.allocator);
	Parent(cppexp.db->functions, &crcpp::Function::parameters, cppexp.db->fields, cppexp.allocator);
	Parent(cppexp.db->classes, &crcpp::Class::enums, cppexp.db->enums, cppexp.allocator);
	Parent(cppexp.db->classes, &crcpp::Class::classes, cppexp.db->classes, cppexp.allocator);
	Parent(cppexp.db->classes, &crcpp::Class::methods, cppexp.db->functions, cppexp.allocator);
	Parent(cppexp.db->classes, &crcpp::Class::fields, cppexp.db->fields, cppexp.allocator);
	Parent(cppexp.db->namespaces, &crcpp::Namespace::namespaces, cppexp.db->namespaces, cppexp.allocator);
	Parent(cppexp.db->namespaces, &crcpp::Namespace::types, cppexp.db->types, cppexp.allocator);
	Parent(cppexp.db->namespaces, &crcpp::Namespace::enums, cppexp.db->enums, cppexp.allocator);
	Parent(cppexp.db->namespaces, &crcpp::Namespace::classes, cppexp.db->classes, cppexp.allocator);
	Parent(cppexp.db->namespaces, &crcpp::Namespace::functions, cppexp.db->functions, cppexp.allocator);

	// Link up any references between primitives
	Link(cppexp.db->fields, &crcpp::Field::type, cppexp.db->types);
	Link(cppexp.db->fields, &crcpp::Field::type, cppexp.db->enums);
	Link(cppexp.db->fields, &crcpp::Field::type, cppexp.db->classes);
	Link(cppexp.db->classes, &crcpp::Class::base_class, cppexp.db->classes);

	// Return parameters are parented to their functions as parameters. Move them from
	// wherever they are in the list and into the return parameter data member.
	AssignReturnParameters(cppexp);

	// Now gather any unparented primitives into the root namespace
	BuildGlobalNamespace(cppexp);

	// Sort any primitive pointer arrays in the database by name hash, ascending. This
	// is to allow fast O(logN) searching of the primitive arrays at runtime with a
	// binary search.
	SortPrimitives(cppexp.db->enums);
	SortPrimitives(cppexp.db->functions);
	SortPrimitives(cppexp.db->classes);
	SortPrimitives(cppexp.db->namespaces);
}


namespace
{
	bool SortFieldByOffset(const crcpp::Field* a, const crcpp::Field* b)
	{
		return a->offset < b->offset;
	}
	bool SortEnumConstantByValue(const crcpp::EnumConstant* a, const crcpp::EnumConstant* b)
	{
		return a->value < b->value;
	}


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

		// Sort parameters by index for viewing
		crcpp::CArray<const crcpp::Field*> sorted_parameters = func.parameters;
		std::sort(sorted_parameters.data(), sorted_parameters.data() + sorted_parameters.size(), SortFieldByOffset);

		for (int i = 0; i < sorted_parameters.size(); i++)
		{
			LogField(*sorted_parameters[i]);
			if (i != sorted_parameters.size() - 1)
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

		// Sort constants by value for viewing
		crcpp::CArray<const crcpp::EnumConstant*> sorted_constants = e.constants;
		std::sort(sorted_constants.data(), sorted_constants.data() + sorted_constants.size(), SortEnumConstantByValue);

		LogPrimitives(sorted_constants);

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

		// Sort fields by offset for viewing
		crcpp::CArray<const crcpp::Field*> sorted_fields = cls.fields;
		std::sort(sorted_fields.data(), sorted_fields.data() + sorted_fields.size(), SortFieldByOffset);

		LogPrimitives(cls.classes);
		LogPrimitives(sorted_fields);
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
	LogPrimitive(cppexp.db->global_namespace);
}
