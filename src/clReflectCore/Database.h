
//
// ===============================================================================
// clReflect, Database.h - Offline representation of the entire Reflection
// Database, built during scanning, merged and then exported to whatever format.
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


#pragma once


#include <string>
#include <vector>
#include <map>
#include <cassert>
#include <clcpp/Core.h>


namespace cldb
{
	//
	// Some handy types
	//
	typedef unsigned char		u8;
	typedef unsigned short		u16;
	typedef unsigned int		u32;


	//
	// Calculate the unique ID for binding a function to its parameters
	//
	struct Field;
	u32 CalculateFunctionUniqueID(const Field* return_parameter, const std::vector<Field>& parameters);


	//
	// A descriptive text name with a unique 32-bit hash value for mapping primitives.
	//
	// Note this new representation requires string copying whenever the name is
	// copied.
	//
	struct Name
	{
		// No-name default constructor
		Name() : hash(0) { }

		// Initialise with hash and string representation
		Name(u32 h, const std::string& t) : hash(h), text(t) { }

		// Fast name comparisons using the hash, assuming there are no collisions
		bool operator == (const Name& rhs) const
		{
			return hash == rhs.hash;
		}
		bool operator != (const Name& rhs) const
		{
			return hash != rhs.hash;
		}

		u32 hash;
		std::string text;
	};
	typedef std::map<u32, Name> NameMap;


	//
	// Rather than create a new Type for "X" vs "const X", bloating the database,
	// this stores the qualifier separately. Additionally, the concept of whether
	// a type is a pointer, reference or not is folded in here as well.
	//
	struct Qualifier
	{
		enum Operator
		{
			VALUE,
			POINTER,
			REFERENCE
		};

		Qualifier()
			: op(VALUE)
			, is_const(false)
		{
		}
		Qualifier(Operator op, bool is_const)
			: op(op)
			, is_const(is_const)
		{
		}

		bool operator == (const Qualifier& rhs) const
		{
			return op == rhs.op && is_const == rhs.is_const;
		}

		Operator op;
		bool is_const;
	};


	//
	// Base class for all types of C++ primitives that are reflected
	//
	struct Primitive
	{
		enum Kind
		{
			KIND_ATTRIBUTE,
			KIND_FLAG_ATTRIBUTE,
			KIND_INT_ATTRIBUTE,
			KIND_FLOAT_ATTRIBUTE,
			KIND_NAME_ATTRIBUTE,
			KIND_TEXT_ATTRIBUTE,
			KIND_TYPE,
			KIND_ENUM_CONSTANT,
			KIND_ENUM,
			KIND_FIELD,
			KIND_FUNCTION,
			KIND_TEMPLATE_TYPE,
			KIND_TEMPLATE,
			KIND_CLASS,
			KIND_NAMESPACE,
		};

		// Constructors for default construction and complete construction
		Primitive(Kind k)
			: kind(k)
		{
		}
		Primitive(Kind k, Name n, Name p)
			: kind(k)
			, name(n)
			, parent(p)
		{
		}

		// Implemented with no operator overloading because chaining them is a pain
		bool Equals(const Primitive& rhs) const
		{
			return
				kind == rhs.kind &&
				name == rhs.name &&
				parent == rhs.parent;
		}

		Kind kind;
		Name name;

		// Parent scope primitive
		Name parent;
	};


	//
	// Base attribute type for collecting different attribute types together
	//
	struct Attribute : public Primitive
	{
		// Constructors for for derived types to call
		Attribute(Kind k)
			: Primitive(k)
		{
		}
		Attribute(Kind k, Name n, Name p)
			: Primitive(k, n, p)
		{
		}
	};


	//
	// Representations of the different types of attribute available. Each attribute has a single
	// value with constructors for default construction and complete construction. Some have
	// non-trivial destructors so all need to be deleted as their own type and not the based type
	// due to the absence of a virtual destructor.
	//
	struct FlagAttribute : public Attribute
	{
		FlagAttribute() : Attribute(Primitive::KIND_FLAG_ATTRIBUTE) { }
		FlagAttribute(Name n, Name p) : Attribute(Primitive::KIND_FLAG_ATTRIBUTE, n, p) { }
	};
	struct IntAttribute : public Attribute
	{
		IntAttribute() : Attribute(Primitive::KIND_INT_ATTRIBUTE) { }
		IntAttribute(Name n, Name p, int v) : Attribute(Primitive::KIND_INT_ATTRIBUTE, n, p), value(v) { }
		bool Equals(const IntAttribute& rhs) const
		{
			return Primitive::Equals(rhs) && value == rhs.value;
		}
		int value;
	};
	struct FloatAttribute : public Attribute
	{
		FloatAttribute() : Attribute(Primitive::KIND_FLOAT_ATTRIBUTE) { }
		FloatAttribute(Name n, Name p, float v) : Attribute(Primitive::KIND_FLOAT_ATTRIBUTE, n, p), value(v) { }
		bool Equals(const FloatAttribute& rhs) const
		{
			return Primitive::Equals(rhs) && value == rhs.value;
		}
		float value;
	};
	struct NameAttribute : public Attribute
	{
		NameAttribute() : Attribute(Primitive::KIND_NAME_ATTRIBUTE) { }
		NameAttribute(Name n, Name p, Name v) : Attribute(Primitive::KIND_NAME_ATTRIBUTE, n, p), value(v) { }
		bool Equals(const NameAttribute& rhs) const
		{
			return Primitive::Equals(rhs) && value == rhs.value;
		}
		Name value;
	};
	struct TextAttribute : public Attribute
	{
		TextAttribute() : Attribute(Primitive::KIND_TEXT_ATTRIBUTE) { }
		TextAttribute(Name n, Name p, const char* v) : Attribute(Primitive::KIND_TEXT_ATTRIBUTE, n, p), value(v) { }
		bool Equals(const TextAttribute& rhs) const
		{
			return Primitive::Equals(rhs) && value == rhs.value;
		}
		std::string value;
	};

	struct TypeInheritance
	{
		TypeInheritance(Name _derived_type, Name _base_type) : derived_type(_derived_type), base_type(_base_type)
		{
		}
		TypeInheritance()
		{
		}

		Name derived_type;
		Name base_type;
	};

	//
	// A basic built-in type that classes/structs can also inherit from
	// Only one base type is supported until it becomes necessary to do otherwise.
	//
	struct Type : public Primitive
	{
		// Constructors for default construction and complete construction, with variants for derived types to call
		Type()
			: Primitive(Primitive::KIND_TYPE)
			, size(0)
		{
		}
		Type(Name n, Name p, u32 s)
			: Primitive(Primitive::KIND_TYPE, n, p)
			, size(s)
		{
		}
		Type(Kind k)
			: Primitive(k)
			, size(0)
		{
		}
		Type(Kind k, Name n, Name p, u32 s) : Primitive(k, n, p), size(s) { }

		bool Equals(const Type& rhs) const
		{
			return Primitive::Equals(rhs) && size == rhs.size;
		}

		// Total size of the type, including alignment
		u32 size;

	};


	//
	// A name/value pair for enumeration constants
	//
	struct EnumConstant : public Primitive
	{
		// Constructors for default construction and complete construction
		EnumConstant()
			: Primitive(Primitive::KIND_ENUM_CONSTANT)
		{
		}
		EnumConstant(Name n, Name p, int v)
			: Primitive(Primitive::KIND_ENUM_CONSTANT, n, p)
			, value(v)
		{
		}

		bool Equals(const EnumConstant& rhs) const
		{
			return Primitive::Equals(rhs) && value == rhs.value;
		}

		// Enumeration constants can have values that are signed/unsigned and of arbitrary width in clang.
		// The standard assures that they're of integral size and is quite vague.
		// For now I'm just assuming they're 32-bit signed.
		int value;
	};


	//
	// A typed enumeration of name/value constant pairs
	//
	struct Enum : public Type
	{
		// Constructors for default construction and complete construction
		Enum()
			: Type(Primitive::KIND_ENUM)
		{
		}
		Enum(Name n, Name p)
			: Type(Primitive::KIND_ENUM, n, p, sizeof(int))
		{
		}
	};


	//
	// Can be either a class/struct field or a function parameter
	//
	struct Field : public Primitive
	{
		// Constructors for default construction and complete construction
		Field()
			: Primitive(Primitive::KIND_FIELD)
			, offset(-1)
			, parent_unique_id(0)
		{
		}
		Field(Name n, Name p, Name t, Qualifier q, int o, u32 uid = 0)
			: Primitive(Primitive::KIND_FIELD, n, p)
			, type(t)
			, qualifier(q)
			, offset(o)
			, parent_unique_id(uid)
		{
		}

		bool Equals(const Field& rhs) const
		{
			return
				Primitive::Equals(rhs) &&
				type == rhs.type &&
				qualifier == rhs.qualifier && 
				offset == rhs.offset &&
				parent_unique_id == rhs.parent_unique_id;
		}

		bool IsFunctionParameter() const
		{
			return parent_unique_id != 0;
		}

		//  Type info
		Name type;
		Qualifier qualifier;

		// Index of the field parameter within its parent function or byte offset within its parent class
		int offset;

		// If this is set then the field is a function parameter
		u32 parent_unique_id;

		// TODO: arrays
		// TODO: bit fields
	};


	//
	// A function or class method with a list of parameters and a return value. When this is a method
	// within a class with calling convention __thiscall, the this parameter is explicitly specified
	// as the first parameter.
	//
	struct Function : public Primitive
	{
		// Constructors for default construction and complete construction
		Function()
			: Primitive(Primitive::KIND_FUNCTION)
			, unique_id(0)
			, address(0)
		{
		}
		Function(Name n, Name p, u32 uid)
			: Primitive(Primitive::KIND_FUNCTION, n, p)
			, unique_id(uid)
			, address(0)
		{
		}

		bool Equals(const Function& rhs) const
		{
			return Primitive::Equals(rhs) && unique_id == rhs.unique_id;
		}

		// An ID unique to this function among other functions that have the same name
		// This allows the function to be referenced accurately by any children
		// All return values are named "return" so a parameter reference won't work here
		u32 unique_id;

		// The address of the function is only used during C++ export at the moment and
		// is not serialised to disk or involved in merging. If at a later date this becomes
		// more tightly integrated to clang/llvm then this will need to be serialised.
		u32 address;
	};


	//
	// Template types are instantiations of templates with fully specified parameters.
	// They don't specify the primitives contained within as these can vary between instantiation,
	// leading to prohibitive memory requirements.
	//
	struct TemplateType : public Type
	{
		// Enough for std::map
		static const int MAX_NB_ARGS = 4;

		TemplateType()
			: Type(KIND_TEMPLATE_TYPE)
		{
			for (int i = 0; i < MAX_NB_ARGS; i++)
				parameter_ptrs[i] = false;
		}
		TemplateType(Name n, Name p)
			: Type(KIND_TEMPLATE_TYPE, n, p, 0)
		{
			for (int i = 0; i < MAX_NB_ARGS; i++)
				parameter_ptrs[i] = false;
		}

		bool Equals(const TemplateType& rhs) const
		{
			if (!Primitive::Equals(rhs))
			{
				return false;
			}

			for (int i = 0; i < MAX_NB_ARGS; i++)
			{
				if (parameter_types[i] != rhs.parameter_types[i] ||
					parameter_ptrs[i] != rhs.parameter_ptrs[i])
					return false;
			}

			return true;
		}


		// Currently only support parameter types that are values or pointers. Template arguments
		// can be anything from integers to function pointers and I really haven't got the time to
		// implement any of this because it will see very limited -- if any -- use from me.
		Name parameter_types[MAX_NB_ARGS];
		bool parameter_ptrs[MAX_NB_ARGS];
	};


	//
	// A template is not a type but a record of a template declaration without specified parameters
	// that instantiations can reference.
	//
	struct Template : public Primitive
	{
		Template()
			: Primitive(Primitive::KIND_TEMPLATE)
		{
		}
		Template(Name n, Name p)
			: Primitive(Primitive::KIND_TEMPLATE, n, p)
		{
		}
	};


	//
	// Description of a C++ struct or class with containing fields, functions, classes, etc.
	//
	struct Class : public Type
	{
		// Constructors for default construction and complete construction
		Class()
			: Type(Primitive::KIND_CLASS)
		{
		}
		Class(Name n, Name p, u32 s)
			: Type(Primitive::KIND_CLASS, n, p, s)
		{
		}

		bool Equals(const Class& rhs) const
		{
			return Type::Equals(rhs);
		}
	};


	//
	// A C++ namespace containing collections of various other reflected C++ primitives
	//
	struct Namespace : public Primitive
	{
		// Constructors for default construction and complete construction
		Namespace()
			: Primitive(Primitive::KIND_NAMESPACE)
		{
		}
		Namespace(Name n, Name p)
			: Primitive(Primitive::KIND_NAMESPACE, n, p)
		{
		}
	};


	//
	// The default DBMap allow multiple primitives of the same type to be stored and
	// quickly looked up, allowing symbol overloading.
	//
	template <typename TYPE>
	struct DBMap : public std::multimap<u32, TYPE>
	{
		typedef std::pair<iterator, iterator> range;
		typedef std::pair<const_iterator, const_iterator> const_range;
	};


	//
	// Point to the runtime addresses of the GetType family of functions so that
	// the values that they return can be patched at runtime.
	//
	struct GetTypeFunctions
	{
		typedef std::map<u32, GetTypeFunctions> MapType;
		u32 get_typename_address;
		u32 get_type_address;
	};


	//
	// Description of a reflected container
	//
	struct ContainerInfo
	{
		enum
		{
			HAS_KEY = 1,
			IS_C_ARRAY = 2
		};

		ContainerInfo()
			: flags(0)
			, count(0)
		{
		}

		// Name of the parent type or field
		Name name;

		// Names of the iterator types responsible for reading and writing elements of the container
		Name read_iterator_type;
		Name write_iterator_type;

		u32 flags;

		// In the case of a C-Array, the number of elements in the array
		u32 count;
	};


	//
	// Container infos are uniquely named - this is a DBMap specialisation to ensure
	// that requirement.
	//
	template <>
	struct DBMap<ContainerInfo> : public std::map<u32, ContainerInfo>
	{
	};

	template<>
	struct DBMap<TypeInheritance> : public std::map<u32, TypeInheritance>
	{
	};

	class Database
	{
	public:
		Database();

		void AddBaseTypePrimitives();

		void AddContainerInfo(const std::string& container, const std::string& read_iterator, const std::string& write_iterator, bool has_key);
		void AddTypeInheritance(Name& derived_type, Name& base_type);

		const Name& GetName(const char* text);
		const Name& GetName(u32 hash) const;

		template <typename TYPE> void Add(const Name& name, const TYPE& object)
		{
			assert(name != Name() && "Unnamed objects not supported");
			DBMap<TYPE>& store = GetDBMap<TYPE>();
			store.insert(DBMap<TYPE>::value_type(name.hash, object));
		}
		template <typename TYPE> void AddPrimitive(const TYPE& prim)
		{
			Add(prim.name, prim);
		}

		template <typename TYPE> const TYPE* GetFirstPrimitive(const char* name_string) const
		{
			// Get the store associated with this type
			const DBMap<TYPE>& store = GetDBMap<TYPE>();

			// Return the first instance of an object with this name
			u32 name = clcpp::internal::HashNameString(name_string);
			DBMap<TYPE>::const_iterator i = store.find(name);
			if (i != store.end())
				return &i->second;

			return 0;
		}

		// A compile-time map to runtime data stores for each primitive type
		template <typename TYPE> DBMap<TYPE>& GetDBMap() { }
		template <> DBMap<Namespace>& GetDBMap() { return m_Namespaces; }
		template <> DBMap<Type>& GetDBMap() { return m_Types; }
		template <> DBMap<Template>& GetDBMap() { return m_Templates; }
		template <> DBMap<TemplateType>& GetDBMap() { return m_TemplateTypes; }
		template <> DBMap<Class>& GetDBMap() { return m_Classes; }
		template <> DBMap<Enum>& GetDBMap() { return m_Enums; }
		template <> DBMap<EnumConstant>& GetDBMap() { return m_EnumConstants; }
		template <> DBMap<Function>& GetDBMap() { return m_Functions; }
		template <> DBMap<Field>& GetDBMap() { return m_Fields; }
		
		// Attribute maps
		template <> DBMap<FlagAttribute>& GetDBMap() { return m_FlagAttributes; }
		template <> DBMap<IntAttribute>& GetDBMap() { return m_IntAttributes; }
		template <> DBMap<FloatAttribute>& GetDBMap() { return m_FloatAttributes; }
		template <> DBMap<NameAttribute>& GetDBMap() { return m_NameAttributes; }
		template <> DBMap<TextAttribute>& GetDBMap() { return m_TextAttributes; }

		// Containers
		template <> DBMap<ContainerInfo>& GetDBMap() { return m_ContainerInfos; }
		template <> DBMap<TypeInheritance>& GetDBMap() { return m_TypeInheritances; }

		// Single pass-through const retrieval of the primitive stores. This strips the const-ness
		// of the 'this' pointer to remove the need to copy-paste the GetPrimitiveStore implementations
		// with const added.
		template <typename TYPE> const DBMap<TYPE>& GetDBMap() const
		{
			return const_cast<Database*>(this)->GetDBMap<TYPE>();
		}

		// All unique, scope-qualified names
		NameMap m_Names;

		// Primitives are owned by the following maps depending upon their type
		DBMap<Namespace> m_Namespaces;
		DBMap<Type> m_Types;
		DBMap<Template> m_Templates;
		DBMap<TemplateType> m_TemplateTypes;
		DBMap<Class> m_Classes;
		DBMap<Enum> m_Enums;
		DBMap<EnumConstant> m_EnumConstants;
		DBMap<Function> m_Functions;
		DBMap<Field> m_Fields;

		// Storage for all attributes of different types
		DBMap<FlagAttribute> m_FlagAttributes;
		DBMap<IntAttribute> m_IntAttributes;
		DBMap<FloatAttribute> m_FloatAttributes;
		DBMap<NameAttribute> m_NameAttributes;
		DBMap<TextAttribute> m_TextAttributes;

		// Store for containers
		DBMap<ContainerInfo> m_ContainerInfos;

		DBMap<TypeInheritance> m_TypeInheritances;

		// All referenced GetType functions per type
		// This is currently not serialised or merged as it's generated during the export
		// stage and discarded after export
		GetTypeFunctions::MapType m_GetTypeFunctions;
	};
}