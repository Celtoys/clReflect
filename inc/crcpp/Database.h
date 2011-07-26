
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

		Primitive() : kind(KIND_NONE), parent(0) { }

		Kind kind;
		Name name;
		const Primitive* parent;
	};


	struct Type : public Primitive
	{
		static const Kind KIND = KIND_TYPE;

		Type() : size(0) { }

		unsigned int size;
	};


	struct EnumConstant : public Primitive
	{
		static const Kind KIND = KIND_ENUM_CONSTANT;

		EnumConstant() : value(0) { }

		int value;
	};


	struct Enum : public Type
	{
		static const Kind KIND = KIND_ENUM;

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

		Field() : type(0), modifier(modifier), is_const(false), offset(0), parent_unique_id(0) { }

		const Type* type;
		Modifier modifier;
		bool is_const;
		int offset;
		unsigned int parent_unique_id;
	};


	struct Function : public Primitive
	{
		static const Kind KIND = KIND_FUNCTION;

		Function() : return_parameter(0), unique_id(0) { }

		unsigned int unique_id;
		const Field* return_parameter;

		// All sorted by name
		CArray<const Field*> parameters;
	};


	struct Class : public Type
	{
		static const Kind KIND = KIND_CLASS;

		Class() : base_class(0) { }

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

		// All sorted by name
		CArray<const Namespace*> namespaces;
		CArray<const Type*> types;
		CArray<const Enum*> enums;
		CArray<const Class*> classes;
		CArray<const Function*> functions;
	};


	//
	// All primitive arrays are sorted in order of increasing name hash. This will perform an
	// O(logN) binary search over the array looking for the name you specify.
	//
	const Primitive* FindPrimitive(const CArray<const Primitive*>& primitives, Name name);


	//
	// Typed wrapper for calling FindPrimitive on arbitrary arrays of primitives. Ensures the
	// types can be cast to Primitive and aliases the arrays to cut down on generated code.
	//
	template <typename TYPE>
	const TYPE* FindPrimitive(const CArray<const TYPE*>& primitives, Name name)
	{
		static_cast<const Primitive*>((const TYPE*)0);
		return (TYPE*)FindPrimitive((const CArray<const Primitive*>&)primitives, name);
	}


	class Database
	{
	public:
		struct FileHeader
		{
			// Initialises the file header to the current supported version
			FileHeader();

			unsigned char signature[7];
			unsigned int version;

			int nb_primitives;
			int nb_names;
			int name_data_size;
		};

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
		CArray<Name> m_Names;

		// Ownership storage of all referenced primitives
		CArray<Type> m_Types;
		CArray<EnumConstant> m_EnumConstants;
		CArray<Enum> m_Enums;
		CArray<Field> m_Fields;
		CArray<Function> m_Functions;
		CArray<Class> m_Classes;
		CArray<Namespace> m_Namespaces;

		// Global map to all primitives in the database
		CArray<const Primitive*> m_Primitives;
	};
}