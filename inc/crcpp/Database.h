
//
// A minimal C++ Reflection database that is built around the notion of being
// read-only once loaded.
//

#pragma once


#include "Core.h"


namespace crcpp
{
	struct Primitive;
	struct Enum;
	struct Class;
	struct IntAttribute;
	struct FloatAttribute;
	struct NameAttribute;
	struct TextAttribute;


	namespace internal
	{
		struct DatabaseMem;

		//
		// All primitive arrays are sorted in order of increasing name hash. This will perform an
		// O(logN) binary search over the array looking for the name you specify.
		//
		const Primitive* FindPrimitive(const CArray<const Primitive*>& primitives, unsigned int hash);
	}


	//
	// A descriptive text name with a unique 32-bit hash value for mapping primitives.
	//
	struct Name
	{
		Name() : hash(0), text(0) { }
		unsigned int hash;
		const char* text;
	};


	//
	// Base class for all types of C++ primitives that are reflected
	//
	struct Primitive
	{
		enum Kind
		{
			KIND_NONE,
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
			KIND_CLASS,
			KIND_NAMESPACE,
		};

		Primitive(Kind k)
			: kind(k)
			, parent(0)
		{
		}

		Kind kind;
		Name name;
		const Primitive* parent;
	};


	//
	// Base attribute type for collecting different attribute types together
	//
	struct Attribute : public Primitive
	{
		static const Kind KIND = KIND_NONE;

		Attribute()
			: Primitive(KIND)
		{
		}

		Attribute(Kind k)
			: Primitive(k)
		{
		}

		// Safe utility functions for casting to derived types
		inline const IntAttribute* AsIntAttribute() const;
		inline const FloatAttribute* AsFloatAttribute() const;
		inline const NameAttribute* AsNameAttribute() const;
		inline const TextAttribute* AsTextAttribute() const;
	};


	//
	// Representations of the different types of attribute available
	//
	struct FlagAttribute : public Attribute
	{
		static const Kind KIND = KIND_FLAG_ATTRIBUTE;
		FlagAttribute() : Attribute(KIND) { }
	};
	struct IntAttribute : public Attribute
	{
		static const Kind KIND = KIND_INT_ATTRIBUTE;
		IntAttribute() : Attribute(KIND), value(0) { }
		int value;
	};
	struct FloatAttribute : public Attribute
	{
		static const Kind KIND = KIND_FLOAT_ATTRIBUTE;
		FloatAttribute() : Attribute(KIND), value(0) { }
		float value;
	};
	struct NameAttribute : public Attribute
	{
		static const Kind KIND = KIND_NAME_ATTRIBUTE;
		NameAttribute() : Attribute(KIND) { }
		Name value;
	};
	struct TextAttribute : public Attribute
	{
		static const Kind KIND = KIND_TEXT_ATTRIBUTE;
		TextAttribute() : Attribute(KIND), value(0) { }
		const char* value;
	};


	//
	// A basic built-in type that classes/structs can also inherit from
	//
	struct Type : public Primitive
	{
		static const Kind KIND = KIND_TYPE;

		Type()
			: Primitive(KIND)
			, size(0)
		{
		}

		Type(Kind k)
			: Primitive(k)
			, size(0)
		{
		}

		// Safe utility functions for casting to derived types
		inline const Enum* AsEnum() const;
		inline const Class* AsClass() const;

		unsigned int size;
	};


	//
	// A name/value pair for enumeration constants
	//
	struct EnumConstant : public Primitive
	{
		static const Kind KIND = KIND_ENUM_CONSTANT;

		EnumConstant()
			: Primitive(KIND)
			, value(0)
		{
		}

		int value;
	};


	//
	// A typed enumeration of name/value constant pairs
	//
	struct Enum : public Type
	{
		static const Kind KIND = KIND_ENUM;

		Enum()
			: Type(KIND)
		{
		}

		// All sorted by name
		CArray<const EnumConstant*> constants;
		CArray<const Attribute*> attributes;
	};


	//
	// Can be either a class/struct field or a function parameter
	//
	struct Field : public Primitive
	{
		static const Kind KIND = KIND_FIELD;

		enum Modifier
		{
			MODIFIER_NONE,
			MODIFIER_VALUE,
			MODIFIER_POINTER,
			MODIFIER_REFERENCE
		};

		Field()
			: Primitive(KIND)
			, type(0)
			, modifier(MODIFIER_NONE)
			, is_const(false)
			, offset(0)
			, parent_unique_id(0)
		{
		}

		const Type* type;
		Modifier modifier;
		bool is_const;
		int offset;
		unsigned int parent_unique_id;

		// All sorted by name
		CArray<const Attribute*> attributes;
	};


	//
	// A function or class method with a list of parameters and a return value. When this is a method
	// within a class with calling convention __thiscall, the this parameter is explicitly specified
	// as the first parameter.
	//
	struct Function : public Primitive
	{
		static const Kind KIND = KIND_FUNCTION;

		Function()
			: Primitive(KIND)
			, return_parameter(0)
			, unique_id(0)
		{
		}

		// Callable address
		unsigned int address;

		unsigned int unique_id;
		const Field* return_parameter;

		// All sorted by name
		CArray<const Field*> parameters;
		CArray<const Attribute*> attributes;
	};


	//
	// Description of a C++ struct or class with containing fields, functions, classes, etc.
	// Only one base class is supported until it becomes necessary to do otherwise.
	//
	struct Class : public Type
	{
		static const Kind KIND = KIND_CLASS;

		Class()
			: Type(KIND)
			, base_class(0)
			, constructor(0)
			, destructor(0)
		{
		}

		const Class* base_class;

		const Function* constructor;
		const Function* destructor;

		// All sorted by name
		CArray<const Enum*> enums;
		CArray<const Class*> classes;
		CArray<const Function*> methods;
		CArray<const Field*> fields;
		CArray<const Attribute*> attributes;
	};


	//
	// A C++ namespace containing collections of various other reflected C++ primitives
	//
	struct Namespace : public Primitive
	{
		static const Kind KIND = KIND_NAMESPACE;

		Namespace()
			: Primitive(KIND)
		{
		}

		// All sorted by name
		CArray<const Namespace*> namespaces;
		CArray<const Type*> types;
		CArray<const Enum*> enums;
		CArray<const Class*> classes;
		CArray<const Function*> functions;
	};


	//
	// Safe utility functions for casting from const Type* to derived types
	//
	inline const Enum* Type::AsEnum() const
	{
		internal::Assert(kind == Enum::KIND);
		return (const Enum*)this;
	}
	inline const Class* Type::AsClass() const
	{
		internal::Assert(kind == Class::KIND);
		return (const Class*)this;
	}


	//
	// Safe utility functions for casting from const Attribute* to derived types
	//
	inline const IntAttribute* Attribute::AsIntAttribute() const
	{
		internal::Assert(kind == IntAttribute::KIND_INT_ATTRIBUTE);
		return (const IntAttribute*)this;
	}
	inline const FloatAttribute* Attribute::AsFloatAttribute() const
	{
		internal::Assert(kind == FloatAttribute::KIND_FLOAT_ATTRIBUTE);
		return (const FloatAttribute*)this;
	}
	inline const NameAttribute* Attribute::AsNameAttribute() const
	{
		internal::Assert(kind == NameAttribute::KIND_NAME_ATTRIBUTE);
		return (const NameAttribute*)this;
	}
	inline const TextAttribute* Attribute::AsTextAttribute() const
	{
		internal::Assert(kind == TextAttribute::KIND_TEXT_ATTRIBUTE);
		return (const TextAttribute*)this;
	}


	//
	// Typed wrapper for calling FindPrimitive on arbitrary arrays of primitives. Ensures the
	// types can be cast to Primitive and aliases the arrays to cut down on generated code.
	//
	template <typename TYPE>
	inline const TYPE* FindPrimitive(const CArray<const TYPE*>& primitives, unsigned int hash)
	{
		// This is both a compile-time and runtime assert
		internal::Assert(TYPE::KIND != Primitive::KIND_NONE);
		return (TYPE*)internal::FindPrimitive((const CArray<const Primitive*>&)primitives, hash);
	}


	class Database
	{
	public:
		Database();
		~Database();

		bool Load(IFile* file);

		// This returns the name as it exists in the name database, with the text pointer
		// pointing to within the database's allocated name data
		Name GetName(const char* text) const;

		// Return either a type, enum or class by hash
		const Type* GetType(unsigned int hash) const;

		const Namespace* GetNamespace(unsigned int hash) const;
		const Function* GetFunction(unsigned int hash) const;

	private:
		// Disable copying
		Database(const Database&);
		Database& operator = (const Database&);

		internal::DatabaseMem* m_DatabaseMem;
	};


	namespace internal
	{
		//
		// Memory-mapped representation of the entire reflection database
		//
		struct DatabaseMem
		{
			DatabaseMem()
				: name_text_data(0)
			{
			}

			// Raw allocation of all null-terminated name strings
			const char* name_text_data;

			// Mapping from hash to text string
			CArray<Name> names;

			// Ownership storage of all referenced primitives
			CArray<Type> types;
			CArray<EnumConstant> enum_constants;
			CArray<Enum> enums;
			CArray<Field> fields;
			CArray<Function> functions;
			CArray<Class> classes;
			CArray<Namespace> namespaces;

			// Raw allocation of all null-terminated text attribute strings
			const char* text_attribute_data;

			// Ownership storage of all attributes
			CArray<FlagAttribute> flag_attributes;
			CArray<IntAttribute> int_attributes;
			CArray<FloatAttribute> float_attributes;
			CArray<NameAttribute> name_attributes;
			CArray<TextAttribute> text_attributes;

			// A list of references to all types, enums and classes for potentially quicker
			// searches during serialisation
			CArray<const Type*> type_primitives;

			// The root namespace that allows you to reach every referenced primitive
			Namespace global_namespace;
		};
	}
}