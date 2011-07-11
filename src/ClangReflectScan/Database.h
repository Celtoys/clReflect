
#pragma once


#include <string>
#include <vector>
#include <map>


namespace crdb
{
	//
	// Some handy types
	//
	typedef unsigned char		u8;
	typedef unsigned short		u16;
	typedef unsigned int		u32;


	//
	// Austin Appleby's MurmurHash 3: http://code.google.com/p/smhasher
	//
	u32 MurmurHash3(const void* key, int len, u32 seed);


	//
	// A convenient way to quickly share and map names within the database
	//
	typedef std::map<u32, std::string> NameMap;
	typedef NameMap::iterator Name;


	//
	// Base class for all types of C++ primitives that are reflected
	//
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

		Primitive(Kind k) : kind(k) { }
		Primitive(Kind k, Name n, Name p) : kind(k), name(n), parent(p) { }

		Kind kind;
		Name name;

		// Parent scope primitive
		Name parent;
	};

	
	struct Namespace : public Primitive
	{
		Namespace() : Primitive(Primitive::KIND_NAMESPACE) { }
		Namespace(Name n, Name p) : Primitive(Primitive::KIND_NAMESPACE, n, p) { }
	};


	//
	// A basic built-in type that classes/structs can also inherit from
	//
	struct Type : public Primitive
	{
		Type() : Primitive(Primitive::KIND_TYPE) { }
		Type(Name n, Name p) : Primitive(Primitive::KIND_TYPE, n, p) { }
		Type(Kind k) : Primitive(k) { }
		Type(Kind k, Name n, Name p) : Primitive(k, n, p) { }
	};


	struct Class : public Type
	{
		Class() : Type(Primitive::KIND_CLASS) { }
		Class(Name n, Name p) : Type(Primitive::KIND_CLASS, n, p) { }

		// list of fields

		// classes derived from
	};


	//
	// An enumeration of name/value constant pairs
	//
	struct Enum : public Type
	{
		Enum() : Type(Primitive::KIND_ENUM) { }
		Enum(Name n, Name p) : Type(Primitive::KIND_ENUM, n, p) { }
	};


	//
	// A name/value pair for enumeration constants
	//
	struct EnumConstant : public Primitive
	{
		EnumConstant() : Primitive(Primitive::KIND_ENUM_CONSTANT) { }
		EnumConstant(Name n, Name p, __int64 v) : Primitive(Primitive::KIND_ENUM_CONSTANT, n, p), value(v) { }

		// Enumeration constants can have values that are signed/unsigned and of arbitrary width.
		// For now I'm just assuming they're 32-bit signed.
		__int64 value;
	};


	//
	// A function or class method with a list of parameters and a return value.
	//
	struct Function : public Primitive
	{
		Function() : Primitive(Primitive::KIND_FUNCTION) { }
		Function(Name n, Name p) : Primitive(Primitive::KIND_FUNCTION, n, p) { }
	};


	struct Field : public Primitive
	{
		enum Modifier
		{
			MODIFIER_VALUE,
			MODIFIER_POINTER,
			MODIFIER_REFERENCE
		};

		Field() : Primitive(Primitive::KIND_FIELD), modifier(MODIFIER_VALUE), is_const(false), index(-1) { }
		Field(Name n, Name p, Name t, Modifier pass, bool c, int i) : Primitive(Primitive::KIND_FIELD, n, p), type(t), modifier(pass), is_const(c), index(i) { }

		Name type;
		Modifier modifier;
		bool is_const;

		int index;

		// TODO: arrays
		// TODO: bit fields
	};


	//
	// Primitives can be named or unnamed, requiring different storage. Named types
	// can be quickly looked up by name, whereas unnamed types need to be linearly
	// traversed to match any required patterns.
	// This object contains storage for both, only used internally by the Database.
	//
	template <typename TYPE>
	struct PrimitiveStore
	{
		typedef std::vector<TYPE> UnnamedStore;
		typedef std::multimap<u32, TYPE> NamedStore;	// Allows overloaded functions/methods
		UnnamedStore unnamed;
		NamedStore named;
	};


	class Database
	{
	public:
		Database();

		Name GetNoName();
		Name GetName(const char* text);

		void AddPrimitive(const Namespace& prim);
		void AddPrimitive(const Type& prim);
		void AddPrimitive(const Class& prim);
		void AddPrimitive(const Enum& prim);
		void AddPrimitive(const EnumConstant& prim);
		void AddPrimitive(const Function& prim);
		void AddPrimitive(const Field& prim);

	private:
		// All unique, scope-qualified names
		NameMap m_Names;

		// Primitives are owned by the following maps depending upon their type
		PrimitiveStore<Namespace> m_Namespaces;
		PrimitiveStore<Type> m_Types;
		PrimitiveStore<Class> m_Classes;
		PrimitiveStore<Enum> m_Enums;
		PrimitiveStore<EnumConstant> m_EnumConstants;
		PrimitiveStore<Function> m_Functions;
		PrimitiveStore<Field> m_Fields;
	};

}