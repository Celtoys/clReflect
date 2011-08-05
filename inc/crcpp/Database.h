
//
// A minimal C++ Reflection database that is built around the notion of being
// read-only once loaded.
//

#pragma once


#include "Core.h"


namespace crcpp
{
	struct Enum;
	struct Class;


	struct Name
	{
		Name() : hash(0), text(0) { }
		unsigned int hash;
		const char* text;
	};


	struct Primitive
	{
		enum Kind
		{
			KIND_NONE,
			KIND_NAMESPACE,
			KIND_TYPE,
			KIND_CLASS,
			KIND_ENUM,
			KIND_ENUM_CONSTANT,
			KIND_FUNCTION,
			KIND_FIELD
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


	struct Enum : public Type
	{
		static const Kind KIND = KIND_ENUM;

		Enum()
			: Type(KIND)
		{
		}

		// All sorted by name
		CArray<const EnumConstant*> constants;
	};


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
	};


	struct Function : public Primitive
	{
		static const Kind KIND = KIND_FUNCTION;

		Function()
			: Primitive(KIND)
			, return_parameter(0)
			, unique_id(0)
		{
		}

		unsigned int unique_id;
		const Field* return_parameter;

		// All sorted by name
		CArray<const Field*> parameters;
	};


	struct Class : public Type
	{
		static const Kind KIND = KIND_CLASS;

		Class()
			: Type(KIND)
			, base_class(0)
		{
		}

		const Class* base_class;

		// All sorted by name
		CArray<const Enum*> enums;
		CArray<const Class*> classes;
		CArray<const Function*> methods;
		CArray<const Field*> fields;
	};


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
		Assert(kind == Enum::KIND);
		return (const Enum*)this;
	}
	inline const Class* Type::AsClass() const
	{
		Assert(kind == Class::KIND);
		return (const Class*)this;
	}


	//
	// All primitive arrays are sorted in order of increasing name hash. This will perform an
	// O(logN) binary search over the array looking for the name you specify.
	//
	const Primitive* FindPrimitiveImpl(const CArray<const Primitive*>& primitives, unsigned int hash);


	//
	// Typed wrapper for calling FindPrimitive on arbitrary arrays of primitives. Ensures the
	// types can be cast to Primitive and aliases the arrays to cut down on generated code.
	//
	template <typename TYPE>
	const TYPE* FindPrimitive(const CArray<const TYPE*>& primitives, unsigned int hash)
	{
		// This is both a compile-time and runtime assert
		Assert(TYPE::KIND != Primitive::KIND_NONE);
		return (TYPE*)FindPrimitiveImpl((const CArray<const Primitive*>&)primitives, hash);
	}


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

		// A list of references to all types, enums and classes for potentially quicker
		// searches during serialisation
		CArray<const Type*> type_primitives;

		// The root namespace that allows you to reach every referenced primitive
		Namespace global_namespace;
	};


	class Database
	{
	public:
		Database();
		~Database();

		bool Load(IFile* file);

		// This returns the name as it exists in the name database, with the text pointer
		// pointing to within the database's allocated name data
		Name GetName(const char* text) const;

		const Type* GetType(unsigned int hash) const;

	private:
		// Disable copying
		Database(const Database&);
		Database& operator = (const Database&);

		DatabaseMem* m_DatabaseMem;
	};
}