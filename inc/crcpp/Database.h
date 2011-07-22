
//
// A minimal C++ Reflection database that is built around the notion of being
// read-only once loaded.
//

#pragma once


#include "Core.h"


namespace crcpp
{
	struct Name
	{
		unsigned int hash;
		const char* text;
	};


	struct Primitive
	{
		enum Kind
		{
			KIND_NAMESPACE,
			KIND_TYPE,
			KIND_CLASS,
			KIND_ENUM,
			KIND_ENUM_CONSTANT,
			KIND_FUNCTION,
			KIND_FIELD
		};

		Kind kind;
		Name name;
		const Primitive* parent;
	};


	struct Type : public Primitive
	{
		static const Kind KIND = KIND_TYPE;
	};


	struct EnumConstant : public Type
	{
		static const Kind KIND = KIND_ENUM_CONSTANT;

		int value;
	};


	struct Enum : public Primitive
	{
		static const Kind KIND = KIND_ENUM;

		ConstArray<EnumConstant> constants;
	};


	struct Field : public Primitive
	{
		static const Kind KIND = KIND_FIELD;

		enum Modifier
		{
			MODIFIER_VALUE,
			MODIFIER_POINTER,
			MODIFIER_REFERENCE
		};

		const Type* type;
		Modifier modifier;
		bool is_const;
		int offset;
	};


	struct Function : public Primitive
	{
		static const Kind KIND = KIND_FUNCTION;

		Field return_parameter;

		// Parameters stored in the order which they must be passed
		ConstArray<Field> parameters;
	};


	struct Class : public Type
	{
		static const Kind KIND = KIND_CLASS;

		const Type* base_class;
		unsigned int size;
		ConstArray<const Enum*> enums;
		ConstArray<const Class*> classes;
		ConstArray<const Function*> methods;
		ConstArray<const Field*> fields;
	};


	struct Namespace : public Primitive
	{
		static const Kind KIND = KIND_NAMESPACE;

		ConstArray<const Namespace*> namespaces;
		ConstArray<const Type*> types;
		ConstArray<const Enum*> enums;
		ConstArray<const Class*> classes;
		ConstArray<const Function*> functions;
	};


	//
	// All primitive arrays are sorted in order of increasing name hash. This will perform an
	// O(logN) binary search over the array looking for the name you specify.
	//
	const Primitive* FindPrimitive(const ConstArray<const Primitive*>& primitives, Name name);


	//
	// Typed wrapper for calling FindPrimitive on arbitrary arrays of primitives. Ensures the
	// types can be cast to Primitive and aliases the arrays to cut down on generated code.
	//
	template <typename TYPE>
	const TYPE* FindPrimitive(const ConstArray<const TYPE*>& primitives, Name name)
	{
		static_cast<const Primitive*>((const TYPE*)0);
		return (TYPE*)FindPrimitive((const ConstArray<const Primitive*>&)primitives, name);
	}


	class Database
	{
	public:
		Database();
		~Database();

		bool Load(const char* filename);

		Name GetName(unsigned int hash) const;

		const Primitive* GetPrimitive(Name name, int& nb_primitives) const;

	private:
		// Disable copying
		Database(const Database&);
		Database& operator = (const Database&);

		// Raw allocation of all null-terminated name strings
		const char* m_NameTextData;

		// Mapping from hash to text string
		ConstArray<Name> m_Names;

		// Ownership storage of all referenced primitives
		ConstArray<Type> m_Types;
		ConstArray<Enum> m_Enums;
		ConstArray<Function> m_Functions;
		ConstArray<Class> m_Classes;
		ConstArray<Namespace> m_Namespaces;

		// Global map to all primitives in the database
		ConstArray<const Primitive*> m_Primitives;
	};
}