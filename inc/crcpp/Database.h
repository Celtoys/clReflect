
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


	struct EnumConstant : public Primitive
	{
		static const Kind KIND = KIND_ENUM_CONSTANT;

		int value;
	};


	struct Enum : public Type
	{
		static const Kind KIND = KIND_ENUM;

		CArray<const EnumConstant*> constants;
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

		Field* return_parameter;
		CArray<const Field*> parameters;
	};


	struct Class : public Type
	{
		static const Kind KIND = KIND_CLASS;

		const Class* base_class;
		unsigned int size;
		CArray<const Enum*> enums;
		CArray<const Class*> classes;
		CArray<const Function*> methods;
		CArray<const Field*> fields;
	};


	struct Namespace : public Primitive
	{
		static const Kind KIND = KIND_NAMESPACE;

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