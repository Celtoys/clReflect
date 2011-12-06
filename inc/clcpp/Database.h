
//
// ===============================================================================
// clReflect, Core.h - The runtime C++ API Reflection Database. As simple as
// possible given the constraint that it's read-only once loaded.
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


#include "Core.h"


namespace clcpp
{
	class Database;
	struct Primitive;
	struct Type;
	struct Enum;
	struct TemplateType;
	struct Class;
	struct IntAttribute;
	struct FloatAttribute;
	struct PrimitiveAttribute;
	struct TextAttribute;


	namespace internal
	{
		struct DatabaseMem;

		//
		// All primitive arrays are sorted in order of increasing name hash. This will perform an
		// O(logN) binary search over the array looking for the name you specify.
		//
		const Primitive* FindPrimitive(const CArray<const Primitive*>& primitives, unsigned int hash);

		//
		// Similar to the previous FindPrimitive, except that it returns a range of matching
		// primitives - useful for searching primitives with names that can be overloaded.
		//
		Range FindOverloadedPrimitive(const CArray<const Primitive*>& primitives, unsigned int hash);
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
			: read_iterator_type(0)
			, write_iterator_type(0)
			, flags(0)
		{
		}

		// Name of the parent type or field
		Name name;

		// Pointers to the iterator types responsible for reading and writing elements of the container
		const Type* read_iterator_type;
		const Type* write_iterator_type;

		unsigned int flags;

		// In the case of a C-Array, the number of elements in the array
		unsigned int count;
	};


	//
	// Base class for all types of C++ primitives that are reflected
	//
	struct Primitive
	{
		enum Kind
		{
			KIND_NONE,
			KIND_ATTRIBUTE,
			KIND_FLAG_ATTRIBUTE,
			KIND_INT_ATTRIBUTE,
			KIND_FLOAT_ATTRIBUTE,
			KIND_PRIMITIVE_ATTRIBUTE,
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

		Primitive(Kind k)
			: kind(k)
			, parent(0)
			, database(0)
		{
		}

		Kind kind;
		Name name;
		const Primitive* parent;

		// Database this primitive belongs to
		Database* database;
	};


	//
	// Base attribute type for collecting different attribute types together
	//
	struct Attribute : public Primitive
	{
		static const Kind KIND = KIND_ATTRIBUTE;

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
		inline const PrimitiveAttribute* AsPrimitiveAttribute() const;
		inline const TextAttribute* AsTextAttribute() const;
	};


	//
	// Representations of the different types of attribute available
	//
	struct FlagAttribute : public Attribute
	{
		static const Kind KIND = KIND_FLAG_ATTRIBUTE;
		FlagAttribute() : Attribute(KIND) { }

		//
		// Flag attributes are always stored in an array of Attribute pointers. Checking
		// to see if an attribute is applied to a primitive involves searching the array
		// looking for the attribute by hash name.
		//
		// For flag attributes that are referenced so often that such a search becomes a
		// performance issue, they are also stored as bit flags in a 32-bit value.
		//
		enum
		{
			// "transient" - These primitives are ignored during serialisation
			TRANSIENT		= 0x01,

			// "nullstr" - The primitive is a null terminated char* pointer representing a string
			NULLSTR			= 0x02,

			// If an attribute starts with "load_" or "save_" then these flags are set to indicate there
			// are custom loading functions assigned
			CUSTOM_LOAD		= 0x04,
			CUSTOM_SAVE		= 0x08,
		};
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
	struct PrimitiveAttribute : public Attribute
	{
		static const Kind KIND = KIND_PRIMITIVE_ATTRIBUTE;
		PrimitiveAttribute() : Attribute(KIND), primitive(0) { }
		const Primitive* primitive;
	};
	struct TextAttribute : public Attribute
	{
		static const Kind KIND = KIND_TEXT_ATTRIBUTE;
		TextAttribute() : Attribute(KIND), value(0) { }
		const char* value;
	};


	//
	// A basic built-in type that classes/structs can also inherit from
	// Only one base type is supported until it becomes necessary to do otherwise.
	//
	struct Type : public Primitive
	{
		static const Kind KIND = KIND_TYPE;

		Type()
			: Primitive(KIND)
			, size(0)
			, ci(0)
		{
		}

		Type(Kind k)
			: Primitive(k)
			, size(0)
			, ci(0)
		{
		}

		// Does this type derive from the specified type, by hash?
		bool DerivesFrom(unsigned int type_name_hash) const
		{
			// Search in immediate bases
			for (int i = 0; i < base_types.size(); i++)
			{
				if (base_types[i]->name.hash == type_name_hash)
					return true;
			}

			// Search up the inheritance tree
			for (int i = 0; i < base_types.size(); i++)
			{
				if (base_types[i]->DerivesFrom(type_name_hash))
					return true;
			}

			return false;
		}

		// Safe utility functions for casting to derived types
		inline const Enum* AsEnum() const;
		inline const TemplateType* AsTemplateType() const;
		inline const Class* AsClass() const;

		// Size of the type in bytes
		unsigned int size;

		// Types this one derives from. Can be either a Class or TemplateType.
		CArray<const Type*> base_types;

		// This is non-null if the type is a registered container
		ContainerInfo* ci;
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
			, flag_attributes(0)
		{
		}

		// All sorted by name
		CArray<const EnumConstant*> constants;
		CArray<const Attribute*> attributes;

		// Bits representing some of the flag attributes in the attribute array
		unsigned int flag_attributes;
	};


	//
	// Can be either a class/struct field or a function parameter
	//
	struct Field : public Primitive
	{
		static const Kind KIND = KIND_FIELD;

		Field()
			: Primitive(KIND)
			, type(0)
			, offset(0)
			, parent_unique_id(0)
			, flag_attributes(0)
			, ci(0)
		{
		}

		bool IsFunctionParameter() const
		{
			return parent_unique_id != 0;
		}

		// Type info
		const Type* type;
		Qualifier qualifier;

		// Index of the field parameter within its parent function or byte offset within its parent class
		int offset;

		// If this is set then the field is a function parameter
		unsigned int parent_unique_id;

		// All sorted by name
		CArray<const Attribute*> attributes;

		// Bits representing some of the flag attributes in the attribute array
		unsigned int flag_attributes;

		// This is non-null if the field is a C-Array of constant size
		ContainerInfo* ci;
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
			, flag_attributes(0)
		{
		}

		// Callable address
		unsigned int address;

		// An ID unique to this function among other functions that have the same name
		// This is not really useful at runtime and exists purely to make the database
		// exporting code simpler.
		unsigned int unique_id;

		const Field* return_parameter;

		// All sorted by name
		CArray<const Field*> parameters;
		CArray<const Attribute*> attributes;

		// Bits representing some of the flag attributes in the attribute array
		unsigned int flag_attributes;
	};


	//
	// Template types are instantiations of templates with fully specified parameters.
	// They don't specify the primitives contained within as these can vary between instantiation,
	// leading to prohibitive memory requirements.
	//
	struct TemplateType : public Type
	{
		static const Kind KIND = KIND_TEMPLATE_TYPE;

		static const int MAX_NB_ARGS = 4;

		TemplateType()
			: Type(KIND)
		{
			for (int i = 0; i < MAX_NB_ARGS; i++)
			{
				parameter_types[i] = 0;
				parameter_ptrs[i] = false;
			}
		}

		// A pointer to the type of each template argument
		const Type* parameter_types[MAX_NB_ARGS];

		// Specifies whether each argument is a pointer
		bool parameter_ptrs[MAX_NB_ARGS];
	};


	//
	// A template is not a type but a record of a template declaration without specified parameters
	// that instantiations can reference.
	//
	struct Template : public Primitive
	{
		static const Kind KIND = KIND_TEMPLATE;

		Template()
			: Primitive(KIND)
		{
		}

		// All sorted by name
		CArray<const TemplateType*> instances;
	};


	//
	// Description of a C++ struct or class with containing fields, functions, classes, etc.
	//
	struct Class : public Type
	{
		static const Kind KIND = KIND_CLASS;

		Class()
			: Type(KIND)
			, constructor(0)
			, destructor(0)
			, flag_attributes(0)
		{
		}

		const Function* constructor;
		const Function* destructor;

		// All sorted by name
		CArray<const Enum*> enums;
		CArray<const Class*> classes;
		CArray<const Function*> methods;
		CArray<const Field*> fields;
		CArray<const Attribute*> attributes;
		CArray<const Template*> templates;

		// Bits representing some of the flag attributes in the attribute array
		unsigned int flag_attributes;
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
		CArray<const Template*> templates;
	};


	//
	// Safe utility functions for casting from const Type* to derived types
	//
	inline const Enum* Type::AsEnum() const
	{
		internal::Assert(kind == Enum::KIND);
		return (const Enum*)this;
	}
	inline const TemplateType* Type::AsTemplateType() const
	{
		internal::Assert(kind == TemplateType::KIND);
		return (const TemplateType*)this;
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
		internal::Assert(kind == IntAttribute::KIND);
		return (const IntAttribute*)this;
	}
	inline const FloatAttribute* Attribute::AsFloatAttribute() const
	{
		internal::Assert(kind == FloatAttribute::KIND);
		return (const FloatAttribute*)this;
	}
	inline const PrimitiveAttribute* Attribute::AsPrimitiveAttribute() const
	{
		internal::Assert(kind == PrimitiveAttribute::KIND);
		return (const PrimitiveAttribute*)this;
	}
	inline const TextAttribute* Attribute::AsTextAttribute() const
	{
		internal::Assert(kind == TextAttribute::KIND);
		return (const TextAttribute*)this;
	}


	//
	// Typed wrappers for calling FindPrimitive/FindOverloadedPrimitive on arbitrary arrays
	// of primitives. Ensures the types can be cast to Primitive and aliases the arrays to
	// cut down on generated code.
	//
	template <typename TYPE>
	inline const TYPE* FindPrimitive(const CArray<const TYPE*>& primitives, unsigned int hash)
	{
		// This is both a compile-time and runtime assert
		internal::Assert(TYPE::KIND != Primitive::KIND_NONE);
		return (TYPE*)internal::FindPrimitive((const CArray<const Primitive*>&)primitives, hash);
	}
	template <typename TYPE>
	inline Range FindOverloadedPrimitive(const CArray<const TYPE*>& primitives, unsigned int hash)
	{
		// This is both a compile-time and runtime assert
		internal::Assert(TYPE::KIND != Primitive::KIND_NONE);
		return internal::FindOverloadedPrimitive((const CArray<const Primitive*>&)primitives, hash);
	}


	class Database
	{
	public:
		enum
		{
			// When a database is loaded, the code assumes that the module doing the loading
			// is the module that generated the database. It will continue to read the GetType
			// patching addresses and modify the data if this flag isn't passed in.
			OPT_DONT_PATCH_GETTYPE = 0x00000001
		};

		Database();
		~Database();

		bool Load(IFile* file, IAllocator* allocator, unsigned int base_address, unsigned int options);

		// This returns the name as it exists in the name database, with the text pointer
		// pointing to within the database's allocated name data
		Name GetName(unsigned int hash) const;
		Name GetName(const char* text) const;

		// Return either a type, enum, template type or class by hash
		const Type* GetType(unsigned int hash) const;

		// Retrieve namespaces using their fully-scoped names
		const Namespace* GetNamespace(unsigned int hash) const;

		// Retrieve templates using their fully-scoped names
		const Template* GetTemplate(unsigned int hash) const;

		// Retrieve functions by their fully-scoped names, with the option of getting
		// a range of matching overloaded functions
		const Function* GetFunction(unsigned int hash) const;
		Range GetOverloadedFunction(unsigned int hash) const;

		bool IsLoaded() const { return m_DatabaseMem != 0; }

	private:
		// Disable copying
		Database(const Database&);
		Database& operator = (const Database&);

		internal::DatabaseMem* m_DatabaseMem;

		// Allocator used to load the database
		IAllocator* m_Allocator;
	};


	namespace internal
	{
		//
		// Point to the runtime addresses of the GetType family of functions so that
		// the values that they return can be patched at runtime.
		//
		struct GetTypeFunctions
		{
			unsigned int type_hash;
			unsigned int get_typename_address;
			unsigned int get_type_address;
		};


		//
		// Memory-mapped representation of the entire reflection database
		//
		struct DatabaseMem
		{
			DatabaseMem()
				: function_base_address(0)
				, name_text_data(0)
			{
			}

			// The address to subtract when rebasing function addresses
			unsigned int function_base_address;

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
			CArray<Template> templates;
			CArray<TemplateType> template_types;
			CArray<Namespace> namespaces;

			// Raw allocation of all null-terminated text attribute strings
			const char* text_attribute_data;

			// Ownership storage of all attributes
			CArray<FlagAttribute> flag_attributes;
			CArray<IntAttribute> int_attributes;
			CArray<FloatAttribute> float_attributes;
			CArray<PrimitiveAttribute> primitive_attributes;
			CArray<TextAttribute> text_attributes;

			// A list of references to all types, enums and classes for potentially quicker
			// searches during serialisation
			CArray<const Type*> type_primitives;

			// A list of all GetType function addresses paired to their type
			CArray<GetTypeFunctions> get_type_functions;

			// A list of all registered containers
			CArray<ContainerInfo> container_infos;

			// The root namespace that allows you to reach every referenced primitive
			Namespace global_namespace;
		};
	}
}