
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//


//
// TODO: This file is very complicated. I'm not sure the complexity benefits of having
// a flat, one-to-many database is paying off here. Investigate changing clReflectScan
// so that it exports a hierarchical database.
//

#include "CppExport.h"
#include "PtrRelocator.h"

#include <clReflectCore/Database.h>
#include <clReflectCore/Logging.h>
#include <clReflectCore/FileUtils.h>

#include <clcpp/clcpp.h>
#include <clcpp/clcpp_internal.h>

#include <algorithm>
#include <string.h>

#if defined(CLCPP_USING_MSVC)
#include <malloc.h>
#else
#include <stdlib.h>
#endif


namespace
{
	// When copying from cldb::Database into clcpp::Database, we copy the 
	// hash values instead of primitive pointers. This requires us to do
	// casting between pointers and hash values.
	// Since in the current
	// implementation, all hash values are 32-bit long, while pointers may
	// be either 32-bit or 64-bit long.  We provide following macros to do
	// casting job.
	#define POINTER_TO_HASH(p) ((cldb::u32) ((clcpp::pointer_type) (p)))

	#define HASH_TO_POINTER(h) ((void*) ((clcpp::pointer_type) h))

	// A basic malloc allocator implementation
	class Malloc : public clcpp::IAllocator
	{
		void* Alloc(clcpp::size_type size)
		{
			return malloc(size);
		}
		void Free(void* ptr)
		{
			free(ptr);
		}
	};


	void BuildNames(const cldb::Database& db, CppExport& cppexp)
	{
		// Allocate the name data
		unsigned int name_data_size = 0;
		for (cldb::NameMap::const_iterator i = db.m_Names.begin(); i != db.m_Names.end(); ++i)
		{
			name_data_size += i->second.text.length() + 1;
		}
		cppexp.db->name_text_data = cppexp.allocator.Alloc<char>(name_data_size);

		// Populate the name data and build the sorted name map
		name_data_size = 0;
		for (cldb::NameMap::const_iterator i = db.m_Names.begin(); i != db.m_Names.end(); ++i)
		{
			char* text_ptr = (char*)(cppexp.db->name_text_data + name_data_size);
			cppexp.name_map[i->first] = text_ptr;
			const cldb::Name& name = i->second;
			strcpy(text_ptr, name.text.c_str());
			name_data_size += name.text.length() + 1;
		}

		// Build the in-memory name array
		unsigned int nb_names = cppexp.name_map.size();
		cppexp.allocator.Alloc(cppexp.db->names, nb_names);
		unsigned int index = 0;
		for (CppExport::NameMap::const_iterator i = cppexp.name_map.begin(); i != cppexp.name_map.end(); ++i)
		{
			clcpp::Name name;
			name.hash = i->first;
			name.text = i->second;
			clcpp::Name& dest = (clcpp::Name&)cppexp.db->names[index++];
			dest = name;
		}
	}


	// Overloads for copying primitives between databases
	void CopyPrimitive(clcpp::Primitive& dest, const cldb::Primitive& src, clcpp::Primitive::Kind kind)
	{
		dest.kind = kind;
		dest.parent = (clcpp::Primitive*) HASH_TO_POINTER(src.parent.hash);
	}
	void CopyPrimitive(clcpp::EnumConstant& dest, const cldb::EnumConstant& src, clcpp::Primitive::Kind kind)
	{
		CopyPrimitive((clcpp::Primitive&)dest, src, kind);
		dest.value = src.value;
	}
	void CopyPrimitive(clcpp::Function& dest, const cldb::Function& src, clcpp::Primitive::Kind kind)
	{
		CopyPrimitive((clcpp::Primitive&)dest, src, kind);
		dest.address = src.address;
		dest.unique_id = src.unique_id;
	}
	void CopyPrimitive(clcpp::Field& dest, const cldb::Field& src, clcpp::Primitive::Kind kind)
	{
		CopyPrimitive((clcpp::Primitive&)dest, src, kind);
		dest.type = (clcpp::Type*) HASH_TO_POINTER(src.type.hash);
		dest.qualifier.is_const = src.qualifier.is_const;
		dest.offset = src.offset;
		dest.parent_unique_id = src.parent_unique_id;
		switch (src.qualifier.op)
		{
		case (cldb::Qualifier::VALUE): dest.qualifier.op = clcpp::Qualifier::VALUE; break;
		case (cldb::Qualifier::POINTER): dest.qualifier.op = clcpp::Qualifier::POINTER; break;
		case (cldb::Qualifier::REFERENCE): dest.qualifier.op = clcpp::Qualifier::REFERENCE; break;
		default: assert(false && "Case not handled");
		}
	}
	void CopyPrimitive(clcpp::Type& dest, const cldb::Type& src, clcpp::Primitive::Kind kind)
	{
		CopyPrimitive((clcpp::Primitive&)dest, src, kind);
		dest.size = src.size;
	}
	void CopyPrimitive(clcpp::TemplateType& dest, const cldb::TemplateType& src, clcpp::Primitive::Kind kind)
	{
		CopyPrimitive((clcpp::Type&)dest, src, kind);
		for (int i = 0; i < cldb::TemplateType::MAX_NB_ARGS; i++)
		{
			dest.parameter_types[i] = (clcpp::Type*) HASH_TO_POINTER(src.parameter_types[i].hash);
			dest.parameter_ptrs[i] = src.parameter_ptrs[i];
		}
	}
	void CopyPrimitive(clcpp::IntAttribute& dest, const cldb::IntAttribute& src, clcpp::Primitive::Kind kind)
	{
		CopyPrimitive((clcpp::Primitive&)dest, src, kind);
		dest.value = src.value;
	}
	void CopyPrimitive(clcpp::FloatAttribute& dest, const cldb::FloatAttribute& src, clcpp::Primitive::Kind kind)
	{
		CopyPrimitive((clcpp::Primitive&)dest, src, kind);
		dest.value = src.value;
	}
	void CopyPrimitive(clcpp::PrimitiveAttribute& dest, const cldb::PrimitiveAttribute& src, clcpp::Primitive::Kind kind)
	{
		CopyPrimitive((clcpp::Primitive&)dest, src, kind);
		dest.primitive = (clcpp::Primitive*) HASH_TO_POINTER(src.value.hash);
	}
	void CopyPrimitive(clcpp::TextAttribute& dest, const cldb::TextAttribute& src, clcpp::Primitive::Kind kind)
	{
		CopyPrimitive((clcpp::Primitive&)dest, src, kind);

		// Store a pointer to the cldb text allocation that will be replaced later
		dest.value = src.value.c_str();
	}

	// Default object copy that assumes it's copying primitives
	// TODO: I can't think of any better way than doing all this
	template <typename CLCPP_TYPE, typename CLDB_TYPE>
	void CopyObject(CLCPP_TYPE& dest, const CLDB_TYPE& src)
	{
		CopyPrimitive(dest, src, CLCPP_TYPE::KIND);
	}

	// CopyObject specialisations
	template <>
	void CopyObject(clcpp::ContainerInfo& dest, const cldb::ContainerInfo& src)
	{
		dest.read_iterator_type = (clcpp::Type*) HASH_TO_POINTER(src.read_iterator_type.hash);
		dest.write_iterator_type = (clcpp::Type*) HASH_TO_POINTER(src.write_iterator_type.hash);
		dest.flags = src.flags;
		dest.count = src.count;
	}


	template <typename CLDB_TYPE, typename CLCPP_TYPE>
	void BuildCArray(CppExport& cppexp, clcpp::CArray<CLCPP_TYPE>& dest, const cldb::Database& db)
	{
		// Allocate enough entries for all primitives
		const cldb::DBMap<CLDB_TYPE>& src = db.GetDBMap<CLDB_TYPE>();
		cppexp.allocator.Alloc(dest, src.size());

		// Copy individually
		int index = 0;
		for (typename cldb::DBMap<CLDB_TYPE>::const_iterator i = src.begin(); i != src.end(); ++i)
		{
			CLCPP_TYPE& dest_prim = dest[index++];
			const CLDB_TYPE& src_prim = i->second;

			// Early reference the text of the name for easier debugging
			dest_prim.name.hash = src_prim.name.hash;
			dest_prim.name.text = cppexp.name_map[src_prim.name.hash];

			// Copy custom data
			CopyObject(dest_prim, src_prim);
		}
	}


	template <typename PARENT_TYPE>
	struct ParentMap
	{
		// Parent and ref-count
		typedef std::pair<PARENT_TYPE*, int> EntryType;

		// Hash to parent and ref-count
		typedef std::multimap<unsigned int, EntryType> MapType;

		// A range within the map
		typedef typename MapType::iterator Iterator;
		typedef std::pair<Iterator, Iterator> Range;

		template <typename TYPE>
		ParentMap(clcpp::CArray<TYPE>& parents)
		{
			// Create a lookup table from hash ID to parent and the number of references the parent has
			for (unsigned int i = 0; i < parents.size; i++)
			{
				PARENT_TYPE& parent = parents[i];
				map.insert(typename MapType::value_type(parent.name.hash, EntryType(&parent, 0)));
			}

			src_start = parents.data;
			src_end = parents.data + parents.size;
		}

		void ResetRefCount()
		{
			for (typename MapType::iterator i = map.begin(); i != map.end(); ++i)
				i->second.second = 0;
		}

		MapType map;

		// Record of the source array
		const PARENT_TYPE* src_start;
		const PARENT_TYPE* src_end;
	};

	// TODO: check if this specialization works
	template <>
	template <>
	ParentMap<clcpp::Field>::ParentMap(clcpp::CArray<clcpp::Field>& parents)
	{
		// This specialisation creates a lookup table specific to fields. Given that field names are
		// not fully-scoped, this makes it impossible to parent them unless their names are combined
		// with their parent's.
		for (unsigned int i = 0; i < parents.size; i++)
		{
			clcpp::Field& field = (clcpp::Field&)parents[i];
			std::string field_name = std::string(field.parent->name.text) + "::" + std::string(field.name.text);
			unsigned int field_hash = clcpp::internal::HashNameString(field_name.c_str());
			map.insert(MapType::value_type(field_hash, EntryType(&field, 0)));
		}

		src_start = parents.data;
		src_end = parents.data + parents.size;
	}


	bool ParentAndChildMatch(const clcpp::Primitive&, const clcpp::Primitive&)
	{
		return true;
	}
	bool ParentAndChildMatch(const clcpp::Function& parent, const clcpp::Field& child)
	{
		return parent.unique_id == child.parent_unique_id;
	}


	template <typename PARENT_TYPE, typename CHILD_TYPE>
	void Parent(ParentMap<PARENT_TYPE>& parents, clcpp::CArray<const CHILD_TYPE*> (PARENT_TYPE::*carray), clcpp::CArray<CHILD_TYPE*>& children, StackAllocator& allocator)
	{
		parents.ResetRefCount();

		// Assign parents and count the references
		for (unsigned int i = 0; i < children.size; i++)
		{
			CHILD_TYPE* child = children[i];

			// Iterate over all matches
			unsigned int hash = POINTER_TO_HASH(child->parent);
			typename ParentMap<PARENT_TYPE>::Range range = parents.map.equal_range(hash);
			for (typename ParentMap<PARENT_TYPE>::Iterator j = range.first; j != range.second; ++j)
			{
				typename ParentMap<PARENT_TYPE>::EntryType& parc = j->second;
				if (ParentAndChildMatch(*parc.first, *child))
				{
					child->parent = parc.first;
					parc.second++;
					break;
				}
			}
		}

		// Allocate the arrays in the parent
		for (typename ParentMap<PARENT_TYPE>::Iterator i = parents.map.begin(); i != parents.map.end(); ++i)
		{
			if (int nb_refs = i->second.second)
			{
				PARENT_TYPE& parent = *i->second.first;
				allocator.Alloc((parent.*carray), nb_refs);

				// To save having to do any further lookups, store the count inside the array
				// at the end
				(parent.*carray)[nb_refs - 1] = 0;
			}
		}

		// Fill in all the arrays
		for (unsigned int i = 0; i < children.size; i++)
		{
			CHILD_TYPE* child = children[i];
			PARENT_TYPE* parent = (PARENT_TYPE*)child->parent;

			// Only process if the parent has been correctly assigned
			if (parent >= parents.src_start && parent < parents.src_end)
			{
				// Locate the current constant count at the end of the array and add this constant
				// to its parent
				// TODO: check for 64-bit(we would only need this when CArray is bigger than ~2GB)
				int nb_constants = (parent->*carray).size;
				int cur_count = (int) ((clcpp::pointer_type)(parent->*carray)[nb_constants - 1]);
				(parent->*carray)[cur_count++] = child;

				// When the last constant gets written, the constant count gets overwritten with
				// the constant pointer and should no longer be updated
				if (cur_count != nb_constants)
					(parent->*carray)[nb_constants - 1] = (CHILD_TYPE*)((clcpp::pointer_type)cur_count);
			}
		}
	}

	template <typename PARENT_TYPE, typename CHILD_TYPE>
	void Parent(ParentMap<PARENT_TYPE>& parents, clcpp::CArray<const CHILD_TYPE*> (PARENT_TYPE::*carray), clcpp::CArray<CHILD_TYPE>& children, StackAllocator& allocator)
	{
		// Create an array of pointers to the children and forward that to the Parent function
		// that acts on arrays of pointers
		Malloc malloc_allocator;
		CArray<CHILD_TYPE*> children_ptrs(children.size, &malloc_allocator);
		for (unsigned int i = 0; i < children_ptrs.size; i++)
			children_ptrs[i] = &children[i];

		Parent(parents, carray, children_ptrs, allocator);
	}


	template <typename TYPE>
	void BuildAttributePtrArray(clcpp::CArray<clcpp::Attribute*>& dest, clcpp::CArray<TYPE>& src, int& pos)
	{
		for (unsigned int i = 0; i < src.size; i++)
			dest[pos++] = &src[i];
	}


	void BuildAttributePtrArray(CppExport& cppexp, clcpp::CArray<clcpp::Attribute*>& attributes)
	{
		// Total count of all attributes
		int size =
			cppexp.db->flag_attributes.size +
			cppexp.db->int_attributes.size +
			cppexp.db->float_attributes.size +
			cppexp.db->primitive_attributes.size +
			cppexp.db->text_attributes.size;

		// Create the destination array
		cppexp.allocator.Alloc(attributes, size);

		// Collect all attribute pointers
		int pos = 0;
		BuildAttributePtrArray(attributes, cppexp.db->flag_attributes, pos);
		BuildAttributePtrArray(attributes, cppexp.db->int_attributes, pos);
		BuildAttributePtrArray(attributes, cppexp.db->float_attributes, pos);
		BuildAttributePtrArray(attributes, cppexp.db->primitive_attributes, pos);
		BuildAttributePtrArray(attributes, cppexp.db->text_attributes, pos);
	}


	void AssignAttributeText(CppExport& cppexp)
	{
		// Count how many bytes are needed to store all attribute text
		int text_size = 0;
		for (unsigned int i = 0; i < cppexp.db->text_attributes.size; i++)
		{
			clcpp::TextAttribute& attr = (clcpp::TextAttribute&)cppexp.db->text_attributes[i];
			text_size += strlen(attr.value) + 1;
		}

		// Allocate memory for them
		cppexp.db->text_attribute_data = cppexp.allocator.Alloc<char>(text_size);

		// Copy all text attribute data to the main store and reassign pointers
		char* pos = (char*)cppexp.db->text_attribute_data;
		for (unsigned int i = 0; i < cppexp.db->text_attributes.size; i++)
		{
			clcpp::TextAttribute& attr = (clcpp::TextAttribute&)cppexp.db->text_attributes[i];
			strcpy(pos, attr.value);
			attr.value = pos;
			pos += strlen(attr.value) + 1;
		}
	}


	template <typename PARENT_TYPE, typename FIELD_TYPE, typename FIELD_OBJECT_TYPE, typename CHILD_TYPE>
	void Link(clcpp::CArray<PARENT_TYPE>& parents, const FIELD_TYPE* (FIELD_OBJECT_TYPE::*field), clcpp::CArray<const CHILD_TYPE*>& children)
	{
		// Create a lookup table from hash ID to child
		typedef std::multimap<unsigned int, const CHILD_TYPE*> ChildMap;
		typedef std::pair<typename ChildMap::iterator, typename ChildMap::iterator> ChildMapRange;
		ChildMap child_map;
		for (unsigned int i = 0; i < children.size; i++)
		{
			const CHILD_TYPE* child = children[i];
			child_map.insert(typename ChildMap::value_type(child->name.hash, child));
		}

		// Link up the pointers
		for (unsigned int i = 0; i < parents.size; i++)
		{
			PARENT_TYPE& parent = parents[i];
			unsigned int hash_id = POINTER_TO_HASH(parent.*field);
			typename ChildMap::iterator j = child_map.find(hash_id);
			if (j != child_map.end())
				(parent.*field) = (FIELD_TYPE*)j->second;
		}
	}


	// Link for array sources
	template <typename PARENT_TYPE, typename FIELD_TYPE, typename FIELD_OBJECT_TYPE, typename CHILD_TYPE, int N>
	void Link(clcpp::CArray<PARENT_TYPE>& parents, const FIELD_TYPE* (FIELD_OBJECT_TYPE::*field)[N], clcpp::CArray<const CHILD_TYPE*>& children)
	{
		// Create a lookup table from hash ID to child
		typedef std::multimap<unsigned int, const CHILD_TYPE*> ChildMap;
		typedef std::pair<typename ChildMap::iterator, typename ChildMap::iterator> ChildMapRange;
		ChildMap child_map;
		for (unsigned int i = 0; i < children.size; i++)
		{
			const CHILD_TYPE* child = children[i];
			child_map.insert(typename ChildMap::value_type(child->name.hash, child));
		}

		// Link up the pointers
		for (unsigned int i = 0; i < parents.size; i++)
		{
			PARENT_TYPE& parent = parents[i];
			for (int j = 0; j < N; j++)
			{
				unsigned int hash_id = POINTER_TO_HASH((parent.*field)[j]);
				typename ChildMap::iterator k = child_map.find(hash_id);
				if (k != child_map.end())
					(parent.*field)[j] = (FIELD_TYPE*)k->second;
			}
		}
	}

	const clcpp::Type* LinkContainerIterator(CppExport& cppexp, const char* container_name, const clcpp::Type* iterator_type)
	{
		if (iterator_type == 0)
			return 0;

		// Alias the type pointer as its hash and lookup the name
		CppExport::NameMap::iterator name = cppexp.name_map.find(POINTER_TO_HASH(iterator_type));
		if (name == cppexp.name_map.end())
		{
			LOG(main, WARNING, "Couldn't find iterator name for '%s'\n", container_name);
			return 0;
		}

		// Lookup the iterator type
		iterator_type = clcpp::FindPrimitive(cppexp.db->type_primitives, name->first);
		if (iterator_type == 0)
			LOG(main, WARNING, "Couldn't find iterator type '%s' for '%s'\n", name->second, container_name);

		return iterator_type;
	}


	void LinkContainerInfos(CppExport& cppexp, ParentMap<clcpp::Field>& field_parents)
	{
		// Build a template map
		typedef std::map<unsigned int, const clcpp::Template*> TemplateMap;
		TemplateMap templates;
		for (unsigned int i = 0; i < cppexp.db->templates.size; i++)
		{
			const clcpp::Template& t = cppexp.db->templates[i];
			templates[t.name.hash] = &t;
		}

		for (unsigned int i = 0; i < cppexp.db->container_infos.size; i++)
		{
			clcpp::ContainerInfo& ci = (clcpp::ContainerInfo&)cppexp.db->container_infos[i];

			// Patch iterator type pointers
			ci.read_iterator_type = LinkContainerIterator(cppexp, ci.name.text, ci.read_iterator_type);
			ci.write_iterator_type = LinkContainerIterator(cppexp, ci.name.text, ci.write_iterator_type);

			// Parent the container info to any types
			const clcpp::Type* parent_type = clcpp::FindPrimitive(cppexp.db->type_primitives, ci.name.hash);
			if (parent_type)
				const_cast<clcpp::Type*>(parent_type)->ci = &ci;

			else
			{
				// Parent the container to all instances of the template it references
				TemplateMap::const_iterator parent_templates = templates.find(ci.name.hash);
				if (parent_templates != templates.end())
				{
					const clcpp::CArray<const clcpp::TemplateType*>& instances = parent_templates->second->instances;
					for (unsigned int j = 0; j < instances.size; j++)
						const_cast<clcpp::TemplateType*>(instances[j])->ci = &ci;
				}

				else
				{
					// Parent the container to any fields
					ParentMap<clcpp::Field>::Iterator parent_field = field_parents.map.find(ci.name.hash);
					if (parent_field != field_parents.map.end())
						const_cast<clcpp::Field*>(parent_field->second.first)->ci = &ci;
				}
			}
		}
	}


	void BuildBaseClassArrays(CppExport& cppexp, const cldb::Database& db)
	{
		// Collect bases types per type (key=derived type, value=vector with base types)
		typedef std::map< clcpp::Type*, std::vector<const clcpp::Type*> > BaseTypesPerTypeMap;
		BaseTypesPerTypeMap base_classes;
		for (cldb::DBMap<cldb::TypeInheritance>::const_iterator i = db.m_TypeInheritances.begin(); i != db.m_TypeInheritances.end(); ++i)
		{
			const cldb::TypeInheritance& inherit = i->second;
			const char* base_type_str = inherit.base_type.text.c_str();
			const char* derived_type_str = inherit.derived_type.text.c_str();

			const clcpp::Type* base_type = clcpp::FindPrimitive(cppexp.db->type_primitives, inherit.base_type.hash);
			if (base_type != 0)
			{
				// Only collect base classes for those derived types which exist
				const clcpp::Type* derived_type = clcpp::FindPrimitive(cppexp.db->type_primitives, inherit.derived_type.hash);
				if (derived_type != 0)
					base_classes[const_cast<clcpp::Type*>(derived_type)].push_back(base_type);
				else
					LOG(main, WARNING, "Derived type '%s' with base '%s' could not be found\n", derived_type_str, base_type_str);
			}
			else
			{
				LOG(main, WARNING, "Base type '%s' of '%s' could not be found\n", base_type_str, derived_type_str);
			}
		}

		// Allocate base type arrays and populate them
		for (BaseTypesPerTypeMap::iterator i = base_classes.begin(); i != base_classes.end(); ++i)
		{
			clcpp::Type* type = i->first;
			cppexp.allocator.Alloc(type->base_types, i->second.size());
			for (unsigned int j = 0; j < type->base_types.size; j++)
				type->base_types[j] = i->second[j];
		}
	}


	int ReturnParameterIndex(const clcpp::CArray<const clcpp::Field*>& parameters)
	{
		// Linear search for the named return value
		static unsigned int return_hash = clcpp::internal::HashNameString("return");
		for (unsigned int i = 0; i < parameters.size; i++)
		{
			if (parameters[i]->name.hash == return_hash)
				return i;
		}
		return -1;
	}


	void AssignReturnParameters(CppExport& cppexp)
	{
		// Iterate over every function that has the return parameter in its parameter list
		for (unsigned int i = 0; i < cppexp.db->functions.size; i++)
		{
			clcpp::Function& func = cppexp.db->functions[i];
			int return_index = ReturnParameterIndex(func.parameters);
			if (return_index == -1)
				continue;

			// Assign the return parameter and remove it from the parameter list
			func.return_parameter = func.parameters[return_index];
			unstable_remove(func.parameters, return_index);
		}
	}


	template <typename TYPE>
	int CountGlobalPrimitives(const clcpp::CArray<TYPE>& primitives)
	{
		// Finding all unparented primitives
		int nb_global_primitives = 0;
		for (unsigned int i = 0; i < primitives.size; i++)
		{
			if (primitives[i].parent == 0)
			{
				nb_global_primitives++;
			}
		}
		return nb_global_primitives;
	}


	template <typename TYPE>
	void GatherGlobalPrimitives(clcpp::CArray<const TYPE*>& dest, const clcpp::CArray<TYPE>& src, StackAllocator& allocator)
	{
		// Allocate enough space for the primitives
		int nb_global_primitives = CountGlobalPrimitives(src);
		allocator.Alloc(dest, nb_global_primitives);

		// Gather all unparented primitives
		int index = 0;
		for (unsigned int i = 0; i < src.size; i++)
		{
			if (src[i].parent == 0)
				dest[index++] = &src[i];
		}
	}


	void BuildGlobalNamespace(CppExport& cppexp)
	{
		GatherGlobalPrimitives(cppexp.db->global_namespace.namespaces, cppexp.db->namespaces, cppexp.allocator);
		GatherGlobalPrimitives(cppexp.db->global_namespace.types, cppexp.db->types, cppexp.allocator);
		GatherGlobalPrimitives(cppexp.db->global_namespace.enums, cppexp.db->enums, cppexp.allocator);
		GatherGlobalPrimitives(cppexp.db->global_namespace.classes, cppexp.db->classes, cppexp.allocator);
		GatherGlobalPrimitives(cppexp.db->global_namespace.functions, cppexp.db->functions, cppexp.allocator);
		GatherGlobalPrimitives(cppexp.db->global_namespace.templates, cppexp.db->templates, cppexp.allocator);
	}


	void GatherTypePrimitives(CppExport& cppexp)
	{
		// Allocate the array
		unsigned int nb_type_primitives = cppexp.db->types.size + cppexp.db->classes.size + cppexp.db->enums.size + cppexp.db->template_types.size;
		cppexp.allocator.Alloc(cppexp.db->type_primitives, nb_type_primitives);

		// Generate references to anything that is a type
		unsigned int index = 0;
		for (unsigned int i = 0; i < cppexp.db->types.size; i++)
			cppexp.db->type_primitives[index++] = &cppexp.db->types[i];
		for (unsigned int i = 0; i < cppexp.db->classes.size; i++)
			cppexp.db->type_primitives[index++] = &cppexp.db->classes[i];
		for (unsigned int i = 0; i < cppexp.db->enums.size; i++)
			cppexp.db->type_primitives[index++] = &cppexp.db->enums[i];
		for (unsigned int i = 0; i < cppexp.db->template_types.size; i++)
			cppexp.db->type_primitives[index++] = &cppexp.db->template_types[i];
	}


	// Sort an array of primitive pointers by name
	bool SortPrimitiveByName(const clcpp::Primitive* a, const clcpp::Primitive* b)
	{
		return a->name.hash < b->name.hash;
	}
	template <typename TYPE>
	void SortPrimitives(clcpp::CArray<const TYPE*>& primitives)
	{
		std::sort(primitives.data, primitives.data + primitives.size, SortPrimitiveByName);
	}


	// Overloads for sorting primitive arrays within a primitive
	void SortPrimitives(clcpp::Enum& primitive)
	{
		SortPrimitives(primitive.base_types);
		SortPrimitives(primitive.constants);
		SortPrimitives(primitive.attributes);
	}
	void SortPrimitives(clcpp::Field& primitive)
	{
		SortPrimitives(primitive.attributes);
	}
	void SortPrimitives(clcpp::Function& primitive)
	{
		SortPrimitives(primitive.parameters);
		SortPrimitives(primitive.attributes);
	}
	void SortPrimitives(clcpp::Class& primitive)
	{
		SortPrimitives(primitive.base_types);
		SortPrimitives(primitive.enums);
		SortPrimitives(primitive.classes);
		SortPrimitives(primitive.methods);
		SortPrimitives(primitive.fields);
		SortPrimitives(primitive.attributes);
		SortPrimitives(primitive.templates);
	}

	void SortPrimitives(clcpp::TemplateType& primitive)
	{
		SortPrimitives(primitive.base_types);
	}

	void SortPrimitives(clcpp::Template& primitive)
	{
		SortPrimitives(primitive.instances);
	}
	void SortPrimitives(clcpp::Namespace& primitive)
	{
		SortPrimitives(primitive.namespaces);
		SortPrimitives(primitive.types);
		SortPrimitives(primitive.enums);
		SortPrimitives(primitive.classes);
		SortPrimitives(primitive.functions);
		SortPrimitives(primitive.templates);
	}


	// Iterate over all provided primitives and sort their primitive arrays
	template <typename TYPE>
	void SortPrimitives(clcpp::CArray<TYPE>& primitives)
	{
		for (unsigned int i = 0; i < primitives.size; i++)
			SortPrimitives(primitives[i]);
	}


	void FindClassConstructors(CppExport& cppexp)
	{
		// Search each class method list for constructors and destructors
		clcpp::CArray<clcpp::Class>& classes = cppexp.db->classes;
		for (unsigned int i = 0; i < classes.size; i++)
		{
			clcpp::Class& cls = classes[i];

			// Methods in a class have fully-scoped names so these need to be constructed first
			// TODO: This isn't ideal for the client :/
			std::string construct_name = std::string(cls.name.text) + "::ConstructObject";
			std::string destruct_name = std::string(cls.name.text) + "::DestructObject";
			unsigned int construct_hash = clcpp::internal::HashNameString(construct_name.c_str());
			unsigned int destruct_hash = clcpp::internal::HashNameString(destruct_name.c_str());

			cls.constructor = clcpp::FindPrimitive(cls.methods, construct_hash);
			cls.destructor = clcpp::FindPrimitive(cls.methods, destruct_hash);
		}
	}


	unsigned int GetFlagAttributeBits(const clcpp::CArray<const clcpp::Attribute*>& attributes)
	{
		// Cache attribute names
		static unsigned int transient_hash = clcpp::internal::HashNameString("transient");
		static unsigned int pre_save_hash = clcpp::internal::HashNameString("pre_save");
		static unsigned int post_load_hash = clcpp::internal::HashNameString("post_load");
		static unsigned int custom_flag = clcpp::internal::HashNameString("custom_flag");
        static unsigned int replicate_hash = clcpp::internal::HashNameString("replicate");

        // Merge all detected common flags
		unsigned int bits = 0;
		for (unsigned int i = 0; i < attributes.size; i++)
		{
			const clcpp::Attribute& attribute = *attributes[i];
			if (attribute.name.hash == transient_hash)
                bits |= attrFlag_Transient;
            else if (attribute.name.hash == pre_save_hash)
                bits |= attrFlag_PreSave;
            else if (attribute.name.hash == post_load_hash)
                bits |= attrFlag_PostLoad;
            else if (attribute.name.hash == replicate_hash)
                bits |= attrFlag_Replicate;
            else if (startswith(attribute.name.text, "load_"))
                bits |= attrFlag_CustomLoad;
            else if (startswith(attribute.name.text, "save_"))
                bits |= attrFlag_CustomSave;

            // A custom flag allows the programmer to manually specify values to OR in
			else if (attribute.name.hash == custom_flag)
			{
				if (attribute.kind == clcpp::Primitive::KIND_INT_ATTRIBUTE)
					bits |= ((clcpp::IntAttribute&)attribute).value;
				else
					LOG(main, WARNING, "Attribute 'custom_flag' must have an integer parameter");
			}
		}

		return bits;
	}


	template <typename TYPE>
	void AddFlagAttributeBits(clcpp::CArray<TYPE>& primitives)
	{
		for (unsigned int i = 0; i < primitives.size; i++)
		{
			TYPE& primitive = primitives[i];
			primitive.flag_attributes = GetFlagAttributeBits(primitive.attributes);
		}
	}


	unsigned int GetInheritedFlagAttributes(clcpp::Class& class_prim)
	{
		static unsigned int custom_flag = clcpp::internal::HashNameString("custom_flag");
		static unsigned int custom_flag_inherit = clcpp::internal::HashNameString("custom_flag_inherit");

		// Collect all custom attribute bits and set the mask determining inheritance
		unsigned int custom_bits = 0, custom_bits_mask = 0;
		for (unsigned int i = 0; i < class_prim.attributes.size; i++)
		{
			const clcpp::Attribute& attribute = *class_prim.attributes[i];
			if (attribute.name.hash == custom_flag && attribute.kind == clcpp::Primitive::KIND_INT_ATTRIBUTE)
				custom_bits |= ((clcpp::IntAttribute&)attribute).value;
			else if (attribute.name.hash == custom_flag_inherit)
				custom_bits_mask = 0xFFFFFFFF;
		}

		return custom_bits & custom_bits_mask;
	}


	unsigned int InheritFlagAttributes(clcpp::Type* primitive)
	{
		unsigned int custom_bits = 0;

		// Depth-first, pull custom bits up from base classes
		for (unsigned int i = 0; i < primitive->base_types.size; i++)
		{
			clcpp::Type* base_type = const_cast<clcpp::Type*>(primitive->base_types[i]);
			custom_bits |= InheritFlagAttributes(base_type);
		}

		// Merge in the bits of this class
		if (primitive->kind == clcpp::Primitive::KIND_CLASS)
		{
			clcpp::Class& class_prim = *(clcpp::Class*)primitive;
			custom_bits |= GetInheritedFlagAttributes(class_prim);
			class_prim.flag_attributes |= custom_bits;
		}

		return custom_bits;
	}


	void InheritFlagAttributes(CppExport& cppexp)
	{
		for (unsigned int i = 0; i < cppexp.db->classes.size; i++)
			InheritFlagAttributes(&cppexp.db->classes[i]);
	}


	void GatherAttributeRefPrimitives(CppExport& cppexp, std::map<cldb::u32, const clcpp::Primitive*>& primitives)
	{
		// Gather the primitives that can be referenced by a PrimitiveAttribute
		for (unsigned int i = 0; i < cppexp.db->type_primitives.size; i++)
		{
			const clcpp::Primitive* primitive = cppexp.db->type_primitives[i];
			primitives[primitive->name.hash] = primitive;
		}
		for (unsigned int i = 0; i < cppexp.db->functions.size; i++)
		{
			const clcpp::Primitive* primitive = &cppexp.db->functions[i];
			primitives[primitive->name.hash] = primitive;
		}
	}


	void AssignPrimitiveAttributes(CppExport& cppexp)
	{
		typedef std::map<cldb::u32, const clcpp::Primitive*> PrimitiveMap;
		PrimitiveMap primitives;
		GatherAttributeRefPrimitives(cppexp, primitives);

		// Point to any primitive attributes and let VerifyPtr clean up anything which can't be assigned
		for (unsigned int i = 0; i < cppexp.db->primitive_attributes.size; i++)
		{
			clcpp::PrimitiveAttribute& attr = cppexp.db->primitive_attributes[i];
			PrimitiveMap::iterator prim_i = primitives.find(POINTER_TO_HASH(attr.primitive));
			if (prim_i != primitives.end())
				attr.primitive = prim_i->second;
		}
	}


	template <typename TYPE>
	const char* VerifyPtr(CppExport& cppexp, const TYPE* const & ptr)
	{
		// Cast to a hash value
		unsigned int hash = POINTER_TO_HASH(ptr);

		// Set the reference to null if it hasn't been resolved
		CppExport::NameMap::const_iterator i = cppexp.name_map.find(hash);
		if (i != cppexp.name_map.end())
		{
			const_cast<const TYPE*&>(ptr) = 0;
			return i->second;
		}

		return 0;
	}

	// Overloads for verifying the pointers of each primitive type
	void VerifyPrimitive(CppExport& cppexp, const clcpp::Primitive& primitive)
	{
		// Note that the arrays within primitives are only populated if the parents of their
		// contents are valid so there is no need to check them for validity; only the
		// individual parent pointers.
		if (const char* unresolved = VerifyPtr(cppexp, primitive.parent))
			LOG(main, WARNING, "Primitive '%s' couldn't find parent reference to '%s'\n", primitive.name.text, unresolved);
	}
	void VerifyPrimitive(CppExport& cppexp, const clcpp::Field& primitive)
	{
		VerifyPrimitive(cppexp, (clcpp::Primitive&)primitive);

		if (const char* unresolved = VerifyPtr(cppexp, primitive.type))
		{
			// Try to give as informative a warning message as possible
			if (primitive.parent)
			{
				switch (primitive.parent->kind)
				{
				case (clcpp::Primitive::KIND_FUNCTION):
					LOG(main, WARNING, "Function parameter '%s' within '%s' couldn't find type reference to '%s'\n", primitive.name.text, primitive.parent->name.text, unresolved);
					break;
				case (clcpp::Primitive::KIND_CLASS):
					LOG(main, WARNING, "Class field '%s' within '%s' couldn't find type reference to '%s'\n", primitive.name.text, primitive.parent->name.text, unresolved);
					break;
				default:
					break;
				}
			}
			else
			{
				LOG(main, WARNING, "Unparented field '%s' couldn't find type reference to '%s'\n", primitive.name.text, unresolved);
			}
		}
	}
	void VerifyPrimitive(CppExport& cppexp, const clcpp::PrimitiveAttribute& primitive)
	{
		if (const char* unresolved = VerifyPtr(cppexp, primitive.primitive))
			LOG(main, WARNING, "Attribute '%s' couldn't find primitive reference to '%s'\n", primitive.name.text, unresolved);
	}
	void VerifyPrimitive(CppExport& cppexp, const clcpp::Type& primitive)
	{
		VerifyPrimitive(cppexp, (clcpp::Primitive&)primitive);

		// Report any warnings with unresolved base class types
		for (unsigned int i = 0; i < primitive.base_types.size; i++)
		{
			if (const char* unresolved = VerifyPtr(cppexp, primitive.base_types[i]))
				LOG(main, WARNING, "Type '%s' couldn't find base type reference to '%s'\n", primitive.name.text, unresolved);
		}
	}
	void VerifyPrimitive(CppExport& cppexp, const clcpp::TemplateType& primitive)
	{
		VerifyPrimitive(cppexp, (clcpp::Type&)primitive);

		for (int i = 0; i < clcpp::TemplateType::MAX_NB_ARGS; i++)
		{
			// Report any warnings with unresolved template parameter types
			if (const char* unresolved = VerifyPtr(cppexp, primitive.parameter_types[i]))
				LOG(main, WARNING, "Template parameter within '%s' couldn't find type reference to '%s'\n", primitive.name.text, unresolved);
		}
	}

	template <typename TYPE>
	void VerifyPrimitives(CppExport& cppexp, const clcpp::CArray<TYPE>& primitives)
	{
		// Verifies all primitives in an array
		for (unsigned int i = 0; i < primitives.size; i++)
			VerifyPrimitive(cppexp, primitives[i]);
	}

	void VerifyPrimitives(CppExport& cppexp)
	{
		VerifyPrimitives(cppexp, cppexp.db->types);
		VerifyPrimitives(cppexp, cppexp.db->enum_constants);
		VerifyPrimitives(cppexp, cppexp.db->enums);
		VerifyPrimitives(cppexp, cppexp.db->fields);
		VerifyPrimitives(cppexp, cppexp.db->functions);
		VerifyPrimitives(cppexp, cppexp.db->classes);
		VerifyPrimitives(cppexp, cppexp.db->templates);
		VerifyPrimitives(cppexp, cppexp.db->template_types);
		VerifyPrimitives(cppexp, cppexp.db->namespaces);
		VerifyPrimitives(cppexp, cppexp.db->flag_attributes);
		VerifyPrimitives(cppexp, cppexp.db->int_attributes);
		VerifyPrimitives(cppexp, cppexp.db->float_attributes);
		VerifyPrimitives(cppexp, cppexp.db->primitive_attributes);
		VerifyPrimitives(cppexp, cppexp.db->text_attributes);
	}


	void RemoveInvalidFields(clcpp::CArray<const clcpp::Field*>& fields)
	{
		// Remove invalid fields, leaving the memory allocated
		for (unsigned int i = 0; i < fields.size; )
		{
			const clcpp::Field* field = fields[i];
			if (field->type == 0)
				stable_remove(fields, i);
			else
				i++;
		}
	}


	void RemoveInvalidFunctions(clcpp::CArray<const clcpp::Function*>& functions)
	{
		for (unsigned int i = 0; i < functions.size; )
		{
			const clcpp::Function* func = functions[i];

			// Search for an invalid parameter
			bool invalid = false;
			for (unsigned int j = 0; j < func->parameters.size; j++)
			{
				if (func->parameters[j]->type == 0)
				{
					invalid = true;
					break;
				}
			}

			invalid |= (func->return_parameter && func->return_parameter->type == 0);

			// Remove from the container if invalid
			if (invalid)
				stable_remove(functions, i);
			else
				i++;
		}
	}


	void IsolateInvalidPrimitives(CppExport& cppexp)
	{
		// Fields and functions within classes
		for (unsigned int i = 0; i < cppexp.db->classes.size; i++)
		{
			clcpp::Class& primitive = cppexp.db->classes[i];
			RemoveInvalidFields(primitive.fields);
			RemoveInvalidFunctions(primitive.methods);
		}

		// Functions within namespaces
		for (unsigned int i = 0; i < cppexp.db->namespaces.size; i++)
		{
			clcpp::Namespace& primitive = cppexp.db->namespaces[i];
			RemoveInvalidFunctions(primitive.functions);
		}
	}
}


bool BuildCppExport(const cldb::Database& db, CppExport& cppexp)
{
	// Allocate the in-memory database
	cppexp.db = cppexp.allocator.Alloc<clcpp::internal::DatabaseMem>(1);
	cppexp.db->function_base_address = cppexp.function_base_address;

	// Build all the name data ready for the client to use and the exporter to debug with
	BuildNames(db, cppexp);

	// Generate a raw clcpp equivalent of the cldb database. At this point no primitives
	// will physically point to or contain each other, but they will reference each other
	// using hash values aliased in their pointers.
	BuildCArray<cldb::Type>(cppexp, cppexp.db->types, db);
	BuildCArray<cldb::EnumConstant>(cppexp, cppexp.db->enum_constants, db);
	BuildCArray<cldb::Enum>(cppexp, cppexp.db->enums, db);
	BuildCArray<cldb::Field>(cppexp, cppexp.db->fields, db);
	BuildCArray<cldb::Function>(cppexp, cppexp.db->functions, db);
	BuildCArray<cldb::Class>(cppexp, cppexp.db->classes, db);
	BuildCArray<cldb::Template>(cppexp, cppexp.db->templates, db);
	BuildCArray<cldb::TemplateType>(cppexp, cppexp.db->template_types, db);
	BuildCArray<cldb::Namespace>(cppexp, cppexp.db->namespaces, db);
	BuildCArray<cldb::FlagAttribute>(cppexp, cppexp.db->flag_attributes, db);
	BuildCArray<cldb::IntAttribute>(cppexp, cppexp.db->int_attributes, db);
	BuildCArray<cldb::FloatAttribute>(cppexp, cppexp.db->float_attributes, db);
	BuildCArray<cldb::PrimitiveAttribute>(cppexp, cppexp.db->primitive_attributes, db);
	BuildCArray<cldb::TextAttribute>(cppexp, cppexp.db->text_attributes, db);
	BuildCArray<cldb::ContainerInfo>(cppexp, cppexp.db->container_infos, db);

	// Now ensure all text data is pointing into the data to be memory mapped
	AssignAttributeText(cppexp);

	// Generate a list of references to all type primitives so that runtime serialisation code
	// can quickly look them up.
	GatherTypePrimitives(cppexp);

	// Create a set of parent maps
	ParentMap<clcpp::Enum> enum_parents(cppexp.db->enums);
	ParentMap<clcpp::Function> function_parents(cppexp.db->functions);
	ParentMap<clcpp::Class> class_parents(cppexp.db->classes);
	ParentMap<clcpp::Namespace> namespace_parents(cppexp.db->namespaces);
	ParentMap<clcpp::Template> template_parents(cppexp.db->templates);

	// Construct the primitive scope hierarchy, pointing primitives at their parents
	// and adding them to the arrays within their parents.
	Parent(enum_parents, &clcpp::Enum::constants, cppexp.db->enum_constants, cppexp.allocator);
	Parent(function_parents, &clcpp::Function::parameters, cppexp.db->fields, cppexp.allocator);
	Parent(class_parents, &clcpp::Class::enums, cppexp.db->enums, cppexp.allocator);
	Parent(class_parents, &clcpp::Class::classes, cppexp.db->classes, cppexp.allocator);
	Parent(class_parents, &clcpp::Class::methods, cppexp.db->functions, cppexp.allocator);
	Parent(class_parents, &clcpp::Class::fields, cppexp.db->fields, cppexp.allocator);
	Parent(class_parents, &clcpp::Class::templates, cppexp.db->templates, cppexp.allocator);
	Parent(namespace_parents, &clcpp::Namespace::namespaces, cppexp.db->namespaces, cppexp.allocator);
	Parent(namespace_parents, &clcpp::Namespace::types, cppexp.db->types, cppexp.allocator);
	Parent(namespace_parents, &clcpp::Namespace::enums, cppexp.db->enums, cppexp.allocator);
	Parent(namespace_parents, &clcpp::Namespace::classes, cppexp.db->classes, cppexp.allocator);
	Parent(namespace_parents, &clcpp::Namespace::functions, cppexp.db->functions, cppexp.allocator);
	Parent(namespace_parents, &clcpp::Namespace::templates, cppexp.db->templates, cppexp.allocator);
	Parent(template_parents, &clcpp::Template::instances, cppexp.db->template_types, cppexp.allocator);

	// Construct field parents after the fields themselves have been parented so that
	// their parents can be used to construct their fully-scoped names
	ParentMap<clcpp::Field> field_parents(cppexp.db->fields);

	// Construct the primitive hierarchy for attributes by first collecting all attributes into
	// a single pointer array
	clcpp::CArray<clcpp::Attribute*> attributes;
	BuildAttributePtrArray(cppexp, attributes);
	Parent(enum_parents, &clcpp::Enum::attributes, attributes, cppexp.allocator);
	Parent(field_parents, &clcpp::Field::attributes, attributes, cppexp.allocator);
	Parent(function_parents, &clcpp::Function::attributes, attributes, cppexp.allocator);
	Parent(class_parents, &clcpp::Class::attributes, attributes, cppexp.allocator);

	// Link up any references between primitives
	Link(cppexp.db->fields, &clcpp::Field::type, cppexp.db->type_primitives);
	Link(cppexp.db->template_types, &clcpp::TemplateType::parameter_types, cppexp.db->type_primitives);

	// Return parameters are parented to their functions as parameters. Move them from
	// wherever they are in the list and into the return parameter data member.
	AssignReturnParameters(cppexp);

	// Gather any unparented primitives into the root namespace
	BuildGlobalNamespace(cppexp);

	// Sort any primitive pointer arrays in the database by name hash, ascending. This
	// is to allow fast O(logN) searching of the primitive arrays at runtime with a
	// binary search.
	SortPrimitives(cppexp.db->enums);
	SortPrimitives(cppexp.db->fields);
	SortPrimitives(cppexp.db->functions);
	SortPrimitives(cppexp.db->classes);
	SortPrimitives(cppexp.db->templates);
	SortPrimitives(cppexp.db->template_types);
	SortPrimitives(cppexp.db->namespaces);
	SortPrimitives(cppexp.db->type_primitives);

	// Container infos need to be parented to their owners and their read/writer iterator
	// pointers need to be linked to their reflected types
	LinkContainerInfos(cppexp, field_parents);

	// Build base classes arrays after the type primitive array has been sorted
	BuildBaseClassArrays(cppexp, db);

	// Each class may have constructor/destructor methods in their method list. Run through
	// each class and make pointers to these in the class. This is done after sorting so that
	// local searches can take advantage of clcpp::FindPrimitive.
	FindClassConstructors(cppexp);

	// For each attribute array in a primitive, calculate a 32-bit value that represents all
	// common flag attributes applied to that primitive.
	AddFlagAttributeBits(cppexp.db->enums);
	AddFlagAttributeBits(cppexp.db->fields);
	AddFlagAttributeBits(cppexp.db->functions);
	AddFlagAttributeBits(cppexp.db->classes);

	// Push any flag attributes marked for inheritance up through class hierarchies
	InheritFlagAttributes(cppexp);

	// Ensure any primitive attributes have their pointers patched
	AssignPrimitiveAttributes(cppexp);

	// Primitives reference each other via their names (hash codes). This code first of all copies
	// hashes into the pointers and then patches them up via lookup. If the input database doesn't
	// contain primitives that others reference then at this point, certain primitives will contain
	// effectively garbage pointers. Do a check here for that and set any garbage pointers to null.
	VerifyPrimitives(cppexp);

	// Remove references to primitives with null pointers in the exported database.
	// Don't want the runtime crashing because it encountered any unexpected null pointers.
	// The memory for the primitives is left allocated, however this shouldn't be an issue
	// if you compile is without warnings!
	IsolateInvalidPrimitives(cppexp);

	return true;
}


void SaveCppExport(CppExport& cppexp, const char* filename)
{
	PtrRelocator relocator(cppexp.allocator.GetData(), cppexp.allocator.GetAllocatedSize());

	// The position of the data member within a CArray is fixed, independent of type
	size_t array_ofs = array_data_offset();

	// ISO C++ 98 standard does not specify the result of applying offsetof on non-POD types,
	// and MSVC and G++ do treat it differently. We need to provide different implementation here.
	#if defined(CLCPP_USING_MSVC)
		size_t global_namespace_offset = offsetof(clcpp::internal::DatabaseMem, global_namespace);
		size_t name_offset_in_primitive = offsetof(clcpp::Primitive, name);
		size_t name_offset_in_container_info = offsetof(clcpp::ContainerInfo, name);
	#else
		clcpp::internal::DatabaseMem dummyDatabaseMem;
		clcpp::Primitive dummyPrimitive(clcpp::Primitive::KIND_NONE);
		clcpp::ContainerInfo dummyContainerInfo;

		size_t global_namespace_offset = ((size_t) (&(dummyDatabaseMem.global_namespace))) - ((size_t) (&dummyDatabaseMem));
		size_t name_offset_in_primitive = ((size_t) (&(dummyPrimitive.name))) - ((size_t) (&dummyPrimitive));
		size_t name_offset_in_container_info = ((size_t) (&(dummyContainerInfo.name))) - ((size_t) (&dummyContainerInfo));
	#endif

	// Construct schemas for all memory-mapped clcpp types

	PtrSchema& schema_database = relocator.AddSchema<clcpp::internal::DatabaseMem>()
		(&clcpp::internal::DatabaseMem::name_text_data)
		(&clcpp::internal::DatabaseMem::names, array_ofs)
		(&clcpp::internal::DatabaseMem::types, array_ofs)
		(&clcpp::internal::DatabaseMem::enum_constants, array_ofs)
		(&clcpp::internal::DatabaseMem::enums, array_ofs)
		(&clcpp::internal::DatabaseMem::fields, array_ofs)
		(&clcpp::internal::DatabaseMem::functions, array_ofs)
		(&clcpp::internal::DatabaseMem::classes, array_ofs)
		(&clcpp::internal::DatabaseMem::template_types, array_ofs)
		(&clcpp::internal::DatabaseMem::templates, array_ofs)
		(&clcpp::internal::DatabaseMem::namespaces, array_ofs)
		(&clcpp::internal::DatabaseMem::text_attribute_data)
		(&clcpp::internal::DatabaseMem::flag_attributes, array_ofs)
		(&clcpp::internal::DatabaseMem::int_attributes, array_ofs)
		(&clcpp::internal::DatabaseMem::float_attributes, array_ofs)
		(&clcpp::internal::DatabaseMem::primitive_attributes, array_ofs)
		(&clcpp::internal::DatabaseMem::text_attributes, array_ofs)
		(&clcpp::internal::DatabaseMem::type_primitives, array_ofs)
		(&clcpp::internal::DatabaseMem::container_infos, array_ofs)
		(&clcpp::Namespace::namespaces, array_ofs + global_namespace_offset)
		(&clcpp::Namespace::types, array_ofs + global_namespace_offset)
		(&clcpp::Namespace::enums, array_ofs + global_namespace_offset)
		(&clcpp::Namespace::classes, array_ofs + global_namespace_offset)
		(&clcpp::Namespace::functions, array_ofs + global_namespace_offset);

	PtrSchema& schema_name = relocator.AddSchema<clcpp::Name>()
		(&clcpp::Name::text);

	PtrSchema& schema_primitive = relocator.AddSchema<clcpp::Primitive>()
		(&clcpp::Name::text, name_offset_in_primitive)
		(&clcpp::Primitive::parent);

	PtrSchema& schema_type = relocator.AddSchema<clcpp::Type>(&schema_primitive)
		(&clcpp::Type::base_types, array_ofs)
		(&clcpp::Type::ci);

	PtrSchema& schema_enum_constant = relocator.AddSchema<clcpp::EnumConstant>(&schema_primitive);

	PtrSchema& schema_enum = relocator.AddSchema<clcpp::Enum>(&schema_type)
		(&clcpp::Enum::constants, array_ofs)
		(&clcpp::Enum::attributes, array_ofs);

	PtrSchema& schema_field = relocator.AddSchema<clcpp::Field>(&schema_primitive)
		(&clcpp::Field::type)
		(&clcpp::Field::attributes, array_ofs)
		(&clcpp::Field::ci);

	PtrSchema& schema_function = relocator.AddSchema<clcpp::Function>(&schema_primitive)
		(&clcpp::Function::return_parameter)
		(&clcpp::Function::parameters, array_ofs)
		(&clcpp::Function::attributes, array_ofs);

	PtrSchema& schema_class = relocator.AddSchema<clcpp::Class>(&schema_type)
		(&clcpp::Class::constructor)
		(&clcpp::Class::destructor)
		(&clcpp::Class::enums, array_ofs)
		(&clcpp::Class::classes, array_ofs)
		(&clcpp::Class::methods, array_ofs)
		(&clcpp::Class::fields, array_ofs)
		(&clcpp::Class::attributes, array_ofs)
		(&clcpp::Class::templates, array_ofs);

	PtrSchema& schema_template_type = relocator.AddSchema<clcpp::TemplateType>(&schema_type)
		(&clcpp::TemplateType::parameter_types, sizeof(void*) * 0)
		(&clcpp::TemplateType::parameter_types, sizeof(void*) * 1)
		(&clcpp::TemplateType::parameter_types, sizeof(void*) * 2)
		(&clcpp::TemplateType::parameter_types, sizeof(void*) * 3);

	PtrSchema& schema_template = relocator.AddSchema<clcpp::Template>(&schema_primitive)
		(&clcpp::Template::instances, array_ofs);

	PtrSchema& schema_namespace = relocator.AddSchema<clcpp::Namespace>(&schema_primitive)
		(&clcpp::Namespace::namespaces, array_ofs)
		(&clcpp::Namespace::types, array_ofs)
		(&clcpp::Namespace::enums, array_ofs)
		(&clcpp::Namespace::classes, array_ofs)
		(&clcpp::Namespace::functions, array_ofs)
		(&clcpp::Namespace::templates, array_ofs);

	PtrSchema& schema_int_attribute = relocator.AddSchema<clcpp::IntAttribute>(&schema_primitive);
	PtrSchema& schema_float_attribute = relocator.AddSchema<clcpp::FloatAttribute>(&schema_primitive);

	PtrSchema& schema_primitive_attribute = relocator.AddSchema<clcpp::PrimitiveAttribute>(&schema_primitive)
		(&clcpp::PrimitiveAttribute::primitive);

	PtrSchema& schema_text_attribute = relocator.AddSchema<clcpp::TextAttribute>(&schema_primitive)
		(&clcpp::TextAttribute::value);

	PtrSchema& schema_ptr = relocator.AddSchema<void*>()(0);

	PtrSchema& schema_container_info = relocator.AddSchema<clcpp::ContainerInfo>()
		(&clcpp::Name::text, name_offset_in_container_info)
		(&clcpp::ContainerInfo::read_iterator_type)
		(&clcpp::ContainerInfo::write_iterator_type);

	// Add pointers from the base database object
	relocator.AddPointers(schema_database, cppexp.db);
	relocator.AddPointers(schema_name, cppexp.db->names);
	relocator.AddPointers(schema_type, cppexp.db->types);
	relocator.AddPointers(schema_enum_constant, cppexp.db->enum_constants);
	relocator.AddPointers(schema_enum, cppexp.db->enums);
	relocator.AddPointers(schema_field, cppexp.db->fields);
	relocator.AddPointers(schema_function, cppexp.db->functions);
	relocator.AddPointers(schema_class, cppexp.db->classes);
	relocator.AddPointers(schema_template_type, cppexp.db->template_types);
	relocator.AddPointers(schema_template, cppexp.db->templates);
	relocator.AddPointers(schema_namespace, cppexp.db->namespaces);
	relocator.AddPointers(schema_primitive, cppexp.db->flag_attributes);
	relocator.AddPointers(schema_int_attribute, cppexp.db->int_attributes);
	relocator.AddPointers(schema_float_attribute, cppexp.db->float_attributes);
	relocator.AddPointers(schema_primitive_attribute, cppexp.db->primitive_attributes);
	relocator.AddPointers(schema_text_attribute, cppexp.db->text_attributes);
	relocator.AddPointers(schema_ptr, cppexp.db->type_primitives);
	relocator.AddPointers(schema_container_info, cppexp.db->container_infos);

	// Add pointers for the array objects within each primitive
	// Note that currently these are expressed as general pointer relocation instructions
	// with a specific "pointer" schema. This is 12 bytes per AddPointers call (which gets
	// into the hundreds/thousands) that could be trimmed a little if a specific pointer
	// relocation instruction was introduced that would cost 8 bytes.
	for (unsigned int i = 0; i < cppexp.db->enums.size; i++)
	{
		relocator.AddPointers(schema_ptr, cppexp.db->enums[i].constants);
		relocator.AddPointers(schema_ptr, cppexp.db->enums[i].attributes);
	}
	for (unsigned int i = 0; i < cppexp.db->fields.size; i++)
	{
		relocator.AddPointers(schema_ptr, cppexp.db->fields[i].attributes);
	}
	for (unsigned int i = 0; i < cppexp.db->functions.size; i++)
	{
		relocator.AddPointers(schema_ptr, cppexp.db->functions[i].parameters);
		relocator.AddPointers(schema_ptr, cppexp.db->functions[i].attributes);
	}
	for (unsigned int i = 0; i < cppexp.db->classes.size; i++)
	{
		clcpp::Class& cls = cppexp.db->classes[i];
		relocator.AddPointers(schema_ptr, cls.enums);
		relocator.AddPointers(schema_ptr, cls.classes);
		relocator.AddPointers(schema_ptr, cls.methods);
		relocator.AddPointers(schema_ptr, cls.fields);
		relocator.AddPointers(schema_ptr, cls.attributes);
		relocator.AddPointers(schema_ptr, cls.templates);
	}
	for (unsigned int i = 0; i < cppexp.db->templates.size; i++)
	{
		relocator.AddPointers(schema_ptr, cppexp.db->templates[i].instances);
	}
	for (unsigned int i = 0; i < cppexp.db->namespaces.size; i++)
	{
		relocator.AddPointers(schema_ptr, cppexp.db->namespaces[i].namespaces);
		relocator.AddPointers(schema_ptr, cppexp.db->namespaces[i].types);
		relocator.AddPointers(schema_ptr, cppexp.db->namespaces[i].enums);
		relocator.AddPointers(schema_ptr, cppexp.db->namespaces[i].classes);
		relocator.AddPointers(schema_ptr, cppexp.db->namespaces[i].functions);
		relocator.AddPointers(schema_ptr, cppexp.db->namespaces[i].templates);
	}

	relocator.AddPointers(schema_ptr, cppexp.db->global_namespace.namespaces);
	relocator.AddPointers(schema_ptr, cppexp.db->global_namespace.types);
	relocator.AddPointers(schema_ptr, cppexp.db->global_namespace.enums);
	relocator.AddPointers(schema_ptr, cppexp.db->global_namespace.classes);
	relocator.AddPointers(schema_ptr, cppexp.db->global_namespace.functions);
	relocator.AddPointers(schema_ptr, cppexp.db->global_namespace.templates);

	for (unsigned int i = 0; i< cppexp.db->type_primitives.size; i++)
	{
		relocator.AddPointers(schema_ptr, cppexp.db->type_primitives[i]->base_types);
	}

	// Make all pointers relative to the start address
	relocator.MakeRelative();

	// Open the output file
	FILE* fp = fopen(filename, "wb");
	if (fp == 0)
	{
		return;
	}

	// Count the total number of pointer offsets
	size_t nb_ptr_offsets = 0;
	const std::vector<PtrSchema*>& schemas = relocator.GetSchemas();
	for (size_t i = 0; i < schemas.size(); i++)
	{
		nb_ptr_offsets += schemas[i]->ptr_offsets.size();
	}

	// Write the header
	clcpp::internal::DatabaseFileHeader header;
	header.nb_ptr_schemas = schemas.size();
	header.nb_ptr_offsets = nb_ptr_offsets;
	const std::vector<PtrRelocation>& relocations = relocator.GetRelocations();
	header.nb_ptr_relocations = relocations.size();
	header.data_size = cppexp.allocator.GetAllocatedSize();
	fwrite(&header, sizeof(header), 1, fp);

	// Write the complete memory map
	fwrite(cppexp.allocator.GetData(), cppexp.allocator.GetAllocatedSize(), 1, fp);

	// Write the stride of each schema and the location of their pointers
	size_t ptrs_offset = 0;
	for (size_t i = 0; i < schemas.size(); i++)
	{
		const PtrSchema& s = *schemas[i];
		size_t nb_ptrs = s.ptr_offsets.size();
		fwrite(&s.stride, sizeof(s.stride), 1, fp);
		fwrite(&ptrs_offset, sizeof(ptrs_offset), 1, fp);
		fwrite(&nb_ptrs, sizeof(nb_ptrs), 1, fp);
		ptrs_offset += nb_ptrs;
	}

	// Write the schema pointer offsets
	for (size_t i = 0; i < schemas.size(); i++)
	{
		const PtrSchema& s = *schemas[i];
		fwrite(&s.ptr_offsets.front(), sizeof(size_t), s.ptr_offsets.size(), fp);
	}

	// Write the relocations
	fwrite(&relocations.front(), sizeof(PtrRelocation), relocations.size(), fp);

	fclose(fp);
}


namespace
{
	bool SortFieldByOffset(const clcpp::Field* a, const clcpp::Field* b)
	{
		return a->offset < b->offset;
	}
	bool SortEnumConstantByValue(const clcpp::EnumConstant* a, const clcpp::EnumConstant* b)
	{
		return a->value < b->value;
	}


	// forward declarations
	void LogPrimitive(const clcpp::Field& field);
	void LogPrimitive(const clcpp::Function& func);
	void LogPrimitive(const clcpp::EnumConstant& constant);
	void LogPrimitive(const clcpp::Enum& e);
	void LogPrimitive(const clcpp::TemplateType& tt);
	void LogPrimitive(const clcpp::Template& t);
	void LogPrimitive(const clcpp::Class& cls);
	void LogPrimitive(const clcpp::Namespace& ns);
	void LogPrimitive(const clcpp::Attribute& attr);


	template <typename TYPE>
	void LogPrimitives(const clcpp::CArray<const TYPE*>& primitives)
	{
		for (unsigned int i = 0; i < primitives.size; i++)
		{
			if (primitives[i] != 0)
				LogPrimitive(*primitives[i]);
			LOG_NEWLINE(cppexp);
		}
	}


	void LogPrimitive(const clcpp::FlagAttribute& attr)
	{
		LOG(cppexp, INFO, "flag: %s", attr.name.text);
	}
	void LogPrimitive(const clcpp::IntAttribute& attr)
	{
		LOG(cppexp, INFO, "int: %s = %d", attr.name.text, attr.value);
	}
	void LogPrimitive(const clcpp::FloatAttribute& attr)
	{
		LOG(cppexp, INFO, "float: %s = %f", attr.name.text, attr.value);
	}
	void LogPrimitive(const clcpp::PrimitiveAttribute& attr)
	{
		if (attr.primitive != 0)
		{
			LOG(cppexp, INFO, "primitive: %s = %s", attr.name.text, attr.primitive->name.text);
		}
		else
		{
			LOG(cppexp, INFO, "primitive: %s = <<UNRESOLVED>>", attr.name.text);
		}
	}
	void LogPrimitive(const clcpp::TextAttribute& attr)
	{
		LOG(cppexp, INFO, "text: %s = %s", attr.name.text, attr.value);
	}
	void LogPrimitive(const clcpp::Attribute& attr)
	{
		switch (attr.kind)
		{
			case clcpp::Primitive::KIND_FLAG_ATTRIBUTE:
				LogPrimitive((clcpp::FlagAttribute&)attr);
				break;
			case clcpp::Primitive::KIND_INT_ATTRIBUTE:
				LogPrimitive((clcpp::IntAttribute&)attr);
				break;
			case clcpp::Primitive::KIND_FLOAT_ATTRIBUTE:
				LogPrimitive((clcpp::FloatAttribute&)attr);
				break;
			case clcpp::Primitive::KIND_PRIMITIVE_ATTRIBUTE:
				LogPrimitive((clcpp::PrimitiveAttribute&)attr);
				break;
			case clcpp::Primitive::KIND_TEXT_ATTRIBUTE:
				LogPrimitive((clcpp::TextAttribute&)attr);
				break;
			default:
				break;
		}
	}


	void LogAttributes(const char* name, const clcpp::CArray<const clcpp::Attribute*>& attributes)
	{
		if (attributes.size)
		{
			LOG(cppexp, INFO, "Attributes for %s", name);
			LOG_NEWLINE(cppexp)
			LOG_PUSH_INDENT(cppexp)
			LogPrimitives(attributes);
			LOG_POP_INDENT(cppexp)
		}
	}


	void LogField(const clcpp::Field& field, bool name = true)
	{
		LOG_APPEND(cppexp, INFO, "%s", field.qualifier.is_const ? "const " : "");
		LOG_APPEND(cppexp, INFO, "%s", field.type ? field.type->name.text : "<<UNRESOLVED TYPE>>");
		LOG_APPEND(cppexp, INFO, "%s", field.qualifier.op == clcpp::Qualifier::POINTER ? "*" :
			field.qualifier.op == clcpp::Qualifier::REFERENCE ? "&" : "");

		if (name)
			LOG_APPEND(cppexp, INFO, " %s", field.name.text);

		if (field.ci != 0)
			LOG_APPEND(cppexp, INFO, "[%d]", field.ci->count);
	}


	void LogPrimitive(const clcpp::Field& field)
	{
		LogAttributes(field.name.text, field.attributes);

		LOG(cppexp, INFO, "");
		LogField(field);
		LOG_APPEND(cppexp, INFO, ";");
	}


	void LogPrimitive(const clcpp::Function& func)
	{
		LogAttributes(func.name.text, func.attributes);

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
		CArray<const clcpp::Field*> sorted_parameters;
		Malloc malloc;
		deep_copy(sorted_parameters, func.parameters, &malloc);
		std::sort(sorted_parameters.data, sorted_parameters.data + sorted_parameters.size, SortFieldByOffset);

		for (unsigned int i = 0; i < sorted_parameters.size; i++)
		{
			if (sorted_parameters[i])
			{
				LogField(*sorted_parameters[i]);
				if (i != sorted_parameters.size - 1)
					LOG_APPEND(cppexp, INFO, ", ");
			}
		}

		LOG_APPEND(cppexp, INFO, ");");
	}


	void LogPrimitive(const clcpp::EnumConstant& constant)
	{
		LOG(cppexp, INFO, "%s = %d,", constant.name.text, constant.value);
	}


	void LogPrimitive(const clcpp::Enum& e)
	{
		LogAttributes(e.name.text, e.attributes);

		LOG(cppexp, INFO, "enum %s\n", e.name.text);
		LOG(cppexp, INFO, "{\n");
		LOG_PUSH_INDENT(cppexp);

		// Sort constants by value for viewing
		clcpp::CArray<const clcpp::EnumConstant*> sorted_constants;
		Malloc malloc;
		deep_copy(sorted_constants, e.constants, &malloc);
		std::sort(sorted_constants.data, sorted_constants.data + sorted_constants.size, SortEnumConstantByValue);

		LogPrimitives(sorted_constants);

		LOG_POP_INDENT(cppexp);
		LOG(cppexp, INFO, "};");
	}


	void LogPrimitive(const clcpp::TemplateType& tt)
	{
		LOG(cppexp, INFO, "class %s", tt.name.text);

		for (unsigned int i=0; i < tt.base_types.size; i++)
			LOG_APPEND(cppexp, INFO, (i==0) ? " : public %s" : ", public %s", tt.base_types[i]->name.text);

		LOG_APPEND(cppexp, INFO, "\n");
	}


	void LogPrimitive(const clcpp::Template& t)
	{
		const char* name = t.name.text;
		LOG(cppexp, INFO, "template %s\n", name);
		LOG(cppexp, INFO, "{\n");
		LOG_PUSH_INDENT(cppexp);

		LogPrimitives(t.instances);

		LOG_POP_INDENT(cppexp);
		LOG(cppexp, INFO, "};");
	}


	void LogPrimitive(const clcpp::Class& cls)
	{
		LogAttributes(cls.name.text, cls.attributes);

		LOG(cppexp, INFO, "class %s", cls.name.text);

		for (unsigned int i=0; i < cls.base_types.size; i++)
			LOG_APPEND(cppexp, INFO, (i==0) ? " : public %s" : ", public %s", cls.base_types[i]->name.text);
		
		LOG_APPEND(cppexp, INFO, "\n");

		LOG(cppexp, INFO, "{\n");
		LOG_PUSH_INDENT(cppexp);

		// Sort fields by offset for viewing
		clcpp::CArray<const clcpp::Field*> sorted_fields;
		Malloc malloc;
		deep_copy(sorted_fields, cls.fields, &malloc);
		std::sort(sorted_fields.data, sorted_fields.data + sorted_fields.size, SortFieldByOffset);

		LogPrimitives(cls.classes);
		LogPrimitives(sorted_fields);
		LogPrimitives(cls.enums);
		LogPrimitives(cls.methods);
		LogPrimitives(cls.templates);

		LOG_POP_INDENT(cppexp);
		LOG(cppexp, INFO, "};");
	}


	void LogPrimitive(const clcpp::Namespace& ns)
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
		LogPrimitives(ns.templates);

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
