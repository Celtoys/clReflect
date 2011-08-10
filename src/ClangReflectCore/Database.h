
#pragma once


#include <string>
#include <vector>
#include <map>
#include <cassert>
#include <crcpp/Core.h>


namespace crdb
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
	// Base class for all types of C++ primitives that are reflected
	//
	struct Primitive
	{
		enum Kind
		{
			KIND_FLAG_ATTRIBUTE,
			KIND_INT_ATTRIBUTE,
			KIND_FLOAT_ATTRIBUTE,
			KIND_NAME_ATTRIBUTE,
			KIND_TEXT_ATTRIBUTE,
			KIND_TYPE,
			KIND_TEMPLATE,
			KIND_TEMPLATE_TYPE,
			KIND_ENUM_CONSTANT,
			KIND_ENUM,
			KIND_FIELD,
			KIND_FUNCTION,
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


	//
	// A basic built-in type that classes/structs can also inherit from
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
	// A template is not a type but a record of a template declaration without specified types
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
	// Template types are instantiations of templates with fully specified parameters.
	// They don't specify the primitives contained within as these can vary between instantiation,
	// leading to prohibitive memory requirements.
	//
	struct TemplateType : public Type
	{
		TemplateType()
			: Type(KIND_TEMPLATE_TYPE)
		{
		}
		TemplateType(Name n, Name p, u32 s)
			: Type(KIND_TEMPLATE_TYPE, n, p, s)
		{
		}

		Name parameter_types[2];
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
		enum Modifier
		{
			MODIFIER_VALUE,
			MODIFIER_POINTER,
			MODIFIER_REFERENCE
		};

		// Constructors for default construction and complete construction
		Field()
			: Primitive(Primitive::KIND_FIELD)
			, modifier(MODIFIER_VALUE)
			, is_const(false)
			, offset(-1)
			, parent_unique_id(0)
		{
		}
		Field(Name n, Name p, Name t, Modifier pass, bool c, int o, u32 uid = 0)
			: Primitive(Primitive::KIND_FIELD, n, p)
			, type(t)
			, modifier(pass)
			, is_const(c)
			, offset(o)
			, parent_unique_id(uid)
		{
		}

		bool Equals(const Field& rhs) const
		{
			return
				Primitive::Equals(rhs) &&
				type == rhs.type &&
				modifier == rhs.modifier &&
				is_const == rhs.is_const &&
				offset == rhs.offset &&
				parent_unique_id == rhs.parent_unique_id;
		}

		Name type;
		Modifier modifier;
		bool is_const;

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
	// Description of a C++ struct or class with containing fields, functions, classes, etc.
	// Only one base class is supported until it becomes necessary to do otherwise.
	//
	struct Class : public Type
	{
		// Constructors for default construction and complete construction
		Class()
			: Type(Primitive::KIND_CLASS)
		{
		}
		Class(Name n, Name p, Name b, u32 s)
			: Type(Primitive::KIND_CLASS, n, p, s)
			, base_class(b)
		{
		}

		bool Equals(const Class& rhs) const
		{
			return Type::Equals(rhs) && base_class == rhs.base_class;
		}

		// Single base class
		Name base_class;
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
	// Primitive stores allow multiple primitives of the same type to be stored and
	// quickly looked up, allowing symbol overloading.
	//
	template <typename TYPE>
	struct PrimitiveStore : public std::multimap<u32, TYPE>
	{
		typedef std::pair<iterator, iterator> range;
		typedef std::pair<const_iterator, const_iterator> const_range;
	};


	class Database
	{
	public:
		Database();

		void AddBaseTypePrimitives();

		const Name& GetName(const char* text);
		const Name& GetName(u32 hash) const;

		template <typename TYPE> void AddPrimitive(const TYPE& prim)
		{
			assert(prim.name != Name() && "Unnamed primitives not supported");
			PrimitiveStore<TYPE>& store = GetPrimitiveStore<TYPE>();
			store.insert(PrimitiveStore<TYPE>::value_type(prim.name.hash, prim));
		}

		template <typename TYPE> const TYPE* GetFirstPrimitive(const char* name_string) const
		{
			// Get the store associated with this type
			const PrimitiveStore<TYPE>& store = GetPrimitiveStore<TYPE>();

			// Return the first instance of an object with this name
			u32 name = crcpp::internal::HashNameString(name_string);
			PrimitiveStore<TYPE>::const_iterator i = store.find(name);
			if (i != store.end())
			{
				return &i->second;
			}

			return 0;
		}

		// A compile-time map to runtime data stores for each primitive type
		template <typename TYPE> PrimitiveStore<TYPE>& GetPrimitiveStore() { }
		template <> PrimitiveStore<Namespace>& GetPrimitiveStore() { return m_Namespaces; }
		template <> PrimitiveStore<Type>& GetPrimitiveStore() { return m_Types; }
		template <> PrimitiveStore<Template>& GetPrimitiveStore() { return m_Templates; }
		template <> PrimitiveStore<TemplateType>& GetPrimitiveStore() { return m_TemplateTypes; }
		template <> PrimitiveStore<Class>& GetPrimitiveStore() { return m_Classes; }
		template <> PrimitiveStore<Enum>& GetPrimitiveStore() { return m_Enums; }
		template <> PrimitiveStore<EnumConstant>& GetPrimitiveStore() { return m_EnumConstants; }
		template <> PrimitiveStore<Function>& GetPrimitiveStore() { return m_Functions; }
		template <> PrimitiveStore<Field>& GetPrimitiveStore() { return m_Fields; }
		
		// Attribute maps
		template <> PrimitiveStore<FlagAttribute>& GetPrimitiveStore() { return m_FlagAttributes; }
		template <> PrimitiveStore<IntAttribute>& GetPrimitiveStore() { return m_IntAttributes; }
		template <> PrimitiveStore<FloatAttribute>& GetPrimitiveStore() { return m_FloatAttributes; }
		template <> PrimitiveStore<NameAttribute>& GetPrimitiveStore() { return m_NameAttributes; }
		template <> PrimitiveStore<TextAttribute>& GetPrimitiveStore() { return m_TextAttributes; }

		// Single pass-through const retrieval of the primitive stores. This strips the const-ness
		// of the 'this' pointer to remove the need to copy-paste the GetPrimitiveStore implementations
		// with const added.
		template <typename TYPE> const PrimitiveStore<TYPE>& GetPrimitiveStore() const
		{
			return const_cast<Database*>(this)->GetPrimitiveStore<TYPE>();
		}

		// All unique, scope-qualified names
		NameMap m_Names;

		// Primitives are owned by the following maps depending upon their type
		PrimitiveStore<Namespace> m_Namespaces;
		PrimitiveStore<Type> m_Types;
		PrimitiveStore<Template> m_Templates;
		PrimitiveStore<TemplateType> m_TemplateTypes;
		PrimitiveStore<Class> m_Classes;
		PrimitiveStore<Enum> m_Enums;
		PrimitiveStore<EnumConstant> m_EnumConstants;
		PrimitiveStore<Function> m_Functions;
		PrimitiveStore<Field> m_Fields;

		// Storage for all attributes of different types
		PrimitiveStore<FlagAttribute> m_FlagAttributes;
		PrimitiveStore<IntAttribute> m_IntAttributes;
		PrimitiveStore<FloatAttribute> m_FloatAttributes;
		PrimitiveStore<NameAttribute> m_NameAttributes;
		PrimitiveStore<TextAttribute> m_TextAttributes;
	};
}