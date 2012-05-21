
//
// ===============================================================================
// clReflect, clcpp.h - Main runtime C++ API header file.
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



// ===============================================================================
//                     Base compiler/platform identification
// ===============================================================================



//
// Checking for compiler type
//
#if defined(_MSC_VER)
	#define CLCPP_USING_MSVC
#else
	#define CLCPP_USING_GNUC
	#if defined(__APPLE__)
		#define CLCPP_USING_GNUC_MAC
	#endif // __APPLE__
#endif // _MSC_VER


//
// Check for operating systems
//
#if defined(_WINDOWS) || defined(_WIN32)
	#define CLCPP_PLATFORM_WINDOWS
#elif defined(__linux__) || defined(__APPLE__)
	#define CLCPP_PLATFORM_POSIX
#endif


//
// Checking if we are running on 32 bit or 64 bit
//
#if defined(CLCPP_USING_MSVC)

	// checking for windows platform
	#if defined(_WIN64)
		#define CLCPP_USING_64_BIT
	#else
		#define CLCPP_USING_32_BIT
	#endif // _WIN64

#else

	// checking for g++ and clang
	#if defined(__LP64__)
		#define CLCPP_USING_64_BIT
	#else
		#define CLCPP_USING_32_BIT
	#endif // __LP64__

#endif // CLCPP_USING_MSVC


//
// Generate a unique symbol with the given prefix
//
#define CLCPP_JOIN2(x, y) x ## y
#define CLCPP_JOIN(x, y) CLCPP_JOIN2(x, y)
#define CLCPP_UNIQUE(x) CLCPP_JOIN(x, __COUNTER__)


//
// Compiler-specific attributes
//
#if defined(CLCPP_USING_MSVC)

	#define CLCPP_EXPORT __declspec(dllexport)
	#define CLCPP_NOINLINE __declspec(noinline)

#elif defined(CLCPP_USING_GNUC)

	#define CLCPP_EXPORT
	#define CLCPP_NOINLINE __attribute__((noinline))

#endif


namespace clcpp
{
    // Defining cross platform size type and pointer type
    //
    // TL;DR version: use pointer_type to holding value casted from a pointer
    // use size_type to hold memory index, offset, length, etc.
    //
    //
    // size_type is a type that can hold any array index, i.e. size_type
    // is used to hold size of a memory block. It is also used to hold the
    // offset of one address from a base address. Remember size_type is an
    // unsigned type, so it only can hold positive offsets.
    //
    // pointer_type is a type for holding address value casting from a pointer.
    //
    // In C99, size_type is exactly size_t, and pointer_type is exactly intptr_t
    // (or uintptr_t), we provide a different name and put it in clcpp here
    // so as not to pollute default namespace.
    //
    // Although the actual types here are the same, the c standard
    // does not enforce these two data to be the same. So we separate them in case
    // we met some platforms fof which these two have different type widths.
    //
    // Since all offsets used in clReflect are positive offsets from a base
    // address, we do not provide ptrdiff_t type here for simplicity. Instead
    // we merge the use cases of ptrdiff_t into size_type(comparing
    // a negative ptrdiff_t type variable with a size_t will be a disaster).
    #if defined(CLCPP_USING_64_BIT)

		typedef unsigned long size_type;
		typedef unsigned long pointer_type;

		// type field used in printf/scanf
		#define CLCPP_SIZE_TYPE_HEX_FORMAT "lX"
		#define CLCPP_POINTER_TYPE_HEX_FORMAT "lX"

	#else

		typedef unsigned int size_type;
		typedef unsigned int pointer_type;

		#define CLCPP_SIZE_TYPE_HEX_FORMAT "X"
		#define CLCPP_POINTER_TYPE_HEX_FORMAT "X"

	#endif // CLCPP_USING_64_BIT

	// cross platform type definitions
	#if defined(CLCPP_USING_MSVC)

		typedef __int64 int64;
		typedef unsigned __int64 uint64;
		typedef unsigned __int32 uint32;

	#else

		typedef long long int64;
		typedef unsigned long long uint64;
		typedef unsigned int uint32;

	#endif // _MSC_VER


	namespace internal
	{
		// Share library (DLL/SO) implementations
		void* LoadSharedLibrary(const char* filename);
		void* GetSharedLibraryFunction(void* handle, const char* function_name);
		void FreeSharedLibrary(void* handle);

		// Gets the load address for the calling module
		clcpp::pointer_type GetLoadAddress();		


		//
		// The C++ standard specifies that use of default placement new does not require inclusion
		// of <new>. However, MSVC2005 disagrees and requires this. Since I don't want any CRT
		// dependencies in the library, I don't want to include that. However, the C++ standard also
		// states that implementing your own default new is illegal.
		//
		// I could just be pragmatic and ignore that (I have done for as long as I've known of the
		// existence of placement new). Or I could do this... wrap pointers in a specific type that
		// forwards to its own placement new, treating it like an allocator that returns the wrapped
		// pointer.
		//
		struct PtrWrapper
		{
		};
	}
}


//
// Placement new for the PtrWrapper logic specified above, which required matching delete
//
inline void* operator new (clcpp::size_type size, const clcpp::internal::PtrWrapper& p)
{
	return (void*)&p;
}
inline void operator delete (void*, const clcpp::internal::PtrWrapper&)
{
}



// ===============================================================================
//                Core Functionality Required by the Runtime C++ API
// ===============================================================================



namespace clcpp
{
	namespace internal
	{
		//
		// Trivial assert for PC only, currently
		//
		inline void Assert(bool expression)
		{
			if (expression == false)
			{
			#ifdef CLCPP_USING_MSVC
				__asm
				{
					int 3h
				}
			#else
                asm("int $0x3\n");
			#endif // CLCPP_USING_MSVC
			}
		}


		//
		// Functions to abstract the calling of an object's constructor and destructor, for
		// debugging and letting the compiler do the type deduction. Makes it a little easier
		// to use the PtrWrapper abstraction.
		//
		template <typename TYPE>
		inline void CallConstructor(TYPE* object)
		{
			new (*(PtrWrapper*)object) TYPE;
		}
		template <typename TYPE>
		inline void CallDestructor(TYPE* object)
		{
			object->~TYPE();
		}


		//
		// Hashes the specified data into a 32-bit value
		//
		unsigned HashData(const void* data, int length, unsigned int seed = 0);


		//
		// Hashes the full string into a 32-bit value
		//
		unsigned int HashNameString(const char* name_string, unsigned int seed = 0);


		//
		// Combines two hashes by using the first one as a seed and hashing the second one
		//
		unsigned int MixHashes(unsigned int a, unsigned int b);
	}


	//
	// Simple allocator interface for abstracting allocations made by the runtime.
	//
	struct IAllocator
	{
		virtual void* Alloc(size_type size) = 0;
		virtual void Free(void* ptr) = 0;
	};


	//
	// Wrapper around a classic C-style array.
	//
	template <typename TYPE>
	class CArray
	{
	public:
		// Initialise an empty array
		CArray()
			: m_Size(0)
			, m_Data(0)
			, m_Allocator(0)
		{
		}

		// Initialise with array count and allocator
		CArray(unsigned int size, IAllocator* allocator)
			: m_Size(size)
			, m_Data(0)
			, m_Allocator(allocator)
		{
			// Allocate and call the constructor for each element
			m_Data = (TYPE*)m_Allocator->Alloc(m_Size * sizeof(TYPE));
			for (unsigned int i = 0; i < m_Size; i++)
				internal::CallConstructor(m_Data + i);
		}

		// Initialise with pre-allocated data
		CArray(void* data, unsigned int size)
			: m_Size(size)
			, m_Data((TYPE*)data)
			, m_Allocator(0)
		{
		}

		~CArray()
		{
			if (m_Allocator)
			{
				// Call the destructor on each element and free the allocated memory
				for (unsigned int i = 0; i < m_Size; i++)
					internal::CallDestructor(m_Data + i);
				m_Allocator->Free(m_Data);
			}
		}

		// A shallow copy of each member in the array
		void shallow_copy(const CArray<TYPE>& rhs)
		{
			m_Size = rhs.m_Size;
			m_Data = rhs.m_Data;
			m_Allocator = rhs.m_Allocator;
		}

		void deep_copy(const CArray<TYPE>& rhs, IAllocator* allocator)
		{
			m_Allocator = allocator;
			internal::Assert(m_Allocator != 0);

			// Allocate and copy each entry
			m_Size = rhs.m_Size;
			m_Data = (TYPE*)m_Allocator->Alloc(m_Size * sizeof(TYPE));
			for (unsigned int i = 0; i < m_Size; i++)
				m_Data[i] = rhs.m_Data[i];
		}

		// Removes an element from the list without reallocating any memory
		// Causes the order of the entries in the list to change
		void unstable_remove(unsigned int index)
		{
			internal::Assert(index < m_Size);
			m_Data[index] = m_Data[m_Size - 1];
			m_Size--;
		}

		int size() const
		{
			return m_Size;
		}

		TYPE* data()
		{
			return m_Data;
		}
		const TYPE* data() const
		{
			return m_Data;
		}

		TYPE& operator [] (unsigned int index)
		{
			internal::Assert(index < m_Size);
			return m_Data[index];
		}
		const TYPE& operator [] (unsigned int index) const
		{
			internal::Assert(index < m_Size);
			return m_Data[index];
		}

		static clcpp::size_type data_offset()
		{
			#if defined(CLCPP_USING_MSVC)
			    return (clcpp::size_type) (&(((CArray<TYPE>*)0)->m_Data));
			#else
				// GCC does not support applying offsetof on non-POD types
				CArray dummy;
				return ((clcpp::size_type) (&(dummy.m_Data))) - ((clcpp::size_type) (&dummy));
			#endif	// CLCPP_USING_MSVC
		}

	private:
		// No need to implement if it's not used - private to ensure they don't get called by accident
		CArray(const CArray& rhs);
		CArray& operator= (const CArray& rhs);

        // TODO: This size is actually count here, so we may not need to convert it to size_type?
		unsigned int m_Size;
		TYPE* m_Data;
		IAllocator* m_Allocator;
	};


	//
	// A simple file interface that the database loader will use. Clients must
	// implement this before they can load a reflection database.
	//
	struct IFile
	{
		// Type and size implied from destination type
		template <typename TYPE> bool Read(TYPE& dest)
		{
			return Read(&dest, sizeof(TYPE));
		}

		// Reads data into an array that has been already allocated
		template <typename TYPE> bool Read(CArray<TYPE>& dest)
		{
			return Read(dest.data(), dest.size() * sizeof(TYPE));
		}

		// Derived classes must implement just the read function, returning
		// true on success, false otherwise.
		virtual bool Read(void* dest, size_type size) = 0;
	};


	//
	// Represents the range [start, end) for iterating over an array
	//
	struct Range
	{
		Range() : first(0), last(0)
		{
		}

		int first;
		int last;
	};
}



// ===============================================================================
//                    Runtime, Read-Only Reflection Database API
// ===============================================================================



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
		bool operator == (const Name& rhs) const { return hash == rhs.hash; }
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

			// If an attribute starts with "load_" or "save_" then these flags are set to indicate there
			// are custom loading functions assigned
			CUSTOM_LOAD		= 0x02,
			CUSTOM_SAVE		= 0x04,
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
        clcpp::size_type size;

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
			, unique_id(0)
			, return_parameter(0)
			, flag_attributes(0)
		{
		}

		// Callable address
        clcpp::pointer_type address;

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
			OPT_DONT_PATCH_GETTYPE = 0x00000001,

			// When a database is loaded, the function pointers stored within are rebased
			// using the load address of the calling module. Use this flag to disable
			// this behaviour.
			OPT_DONT_REBASE_FUNCTIONS = 0x00000002,
		};

		Database();
		~Database();

		bool Load(IFile* file, IAllocator* allocator, unsigned int options);

		// This returns the name as it exists in the name database, with the text pointer
		// pointing to within the database's allocated name data
		Name GetName(unsigned int hash) const;
		Name GetName(const char* text) const;

		// Return either a type, enum, template type or class by hash
		const Type* GetType(unsigned int hash) const;

		// Retrieve namespaces using their fully-scoped names
		const Namespace* GetNamespace(unsigned int hash) const;

		// Retrieve the global namespace, that allows you to reach every primitive
		const Namespace* GetGlobalNamespace() const;

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
            clcpp::pointer_type get_typename_address;
            clcpp::pointer_type get_type_address;
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
            clcpp::pointer_type function_base_address;

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


		//
		// Header for binary database file
		//
		struct DatabaseFileHeader
		{
			// Initialises the file header to the current supported version
			DatabaseFileHeader()
				: signature0('pclc')
				, signature1('\0bdp')
				, version(2)
				, nb_ptr_schemas(0)
				, nb_ptr_offsets(0)
				, nb_ptr_relocations(0)
				, data_size(0)
			{
			}

			// Signature and version numbers for verifying header integrity
			// TODO: add check to pervent loading a 64-bit database from 32-bit
			// runtime system, or vice versa
			unsigned int signature0;
			unsigned int signature1;
			unsigned int version;

			int nb_ptr_schemas;
			int nb_ptr_offsets;
			int nb_ptr_relocations;

			clcpp::size_type data_size;

			// TODO: CRC verify?
		};
	}
}



// ===============================================================================
//              Macros for Tagging C++ Code with Reflection Metadata
// ===============================================================================



// This is a customized macro added into clReflectScan, we use this to distinguish a clReflect scanning
// from a normal compiling using clang as compiler.
// This can help cut normal compiling time since starting from XCode 4.3, clang has become the official
// compiler on Mac OS X
#ifdef __clcpp_parse__


	//
	// Injects a unique structure within the clcpp_internal namespace that only the Clang frontend
	// can see so that it can register the specified symbol for reflection.
	// Can only be called from the global namespace and results in the primitive and any children
	// being fully reflected.
	//
	#define clcpp_reflect(name)						\
													\
		namespace clcpp_internal					\
		{											\
			__attribute__((annotate("full-"#name)))	\
			struct CLCPP_UNIQUE(cldb_reflect) { };	\
		}


	//
	// Similar to clcpp_reflect with the only difference being that the primitive being specified
	// is being partially reflected. Anything that is a child of that primitive has to be
	// explicitly reflected as a result.
	//
	#define clcpp_reflect_part(name)				\
													\
		namespace clcpp_internal					\
		{											\
			__attribute__((annotate("part-"#name)))	\
			struct CLCPP_UNIQUE(cldb_reflect) { };	\
		}


	//
	// A container must have iterators if you want to use reflection to inspect it. Call this from
	// the global namespace in the neighbourhood of any iterator implementations and it will
	// partially reflect the iterators and allow the parent container to be used with reflection.
	//
	#define clcpp_container_iterators(container, read_iterator, write_iterator, keyinfo)							\
		clcpp_reflect_part(read_iterator)																			\
		clcpp_reflect_part(write_iterator)																			\
		namespace clcpp_internal																					\
		{																											\
			__attribute__((annotate("container-" #container "-" #read_iterator "-" #write_iterator "-" #keyinfo)))	\
			struct CLCPP_UNIQUE(container_info) { };																\
		}


	#define clcpp_attr(...) __attribute__((annotate("attr:" #__VA_ARGS__)))
	#define clcpp_push_attr(...) struct CLCPP_UNIQUE(push_attr) { } __attribute__((annotate(#__VA_ARGS__)));
	#define clcpp_pop_attr(...) struct CLCPP_UNIQUE(pop_attr) { } __attribute__((annotate(#__VA_ARGS__)));


	//
	// Clang does not need to see these
	//
	#define clcpp_impl_class(scoped_type)


#else


	//
	// The main compiler does not need to see these
	//
	#define clcpp_reflect(name)
	#define clcpp_reflect_part(name)
	#define clcpp_container_iterators(container, read_iterator, write_iterator, keyinfo)
	#define clcpp_attr(...)
	#define clcpp_push_attr(...)
	#define clcpp_pop_attr(...)


	//
	// Introduces overloaded construction and destruction functions into the clcpp::internal
	// namespace for the type you specify. These functions end up in the list of methods
	// in the specified type for easy access.
	// This can only be used from global namespace.
	//
	#define clcpp_impl_class(type)								\
																\
		CLCPP_EXPORT void clcppConstructObject(type* object)	\
		{														\
			clcpp::internal::CallConstructor(object);			\
		}														\
		CLCPP_EXPORT void clcppDestructObject(type* object)		\
		{														\
			clcpp::internal::CallDestructor(object);			\
		}														\


#endif



// ===============================================================================
//                  Constant-time, String-less Typeof Operator
// ===============================================================================



namespace clcpp
{
	//
	// GetTypeNameHash and GetType are clReflect's implementation of a constant-time,
	// string-less typeof operator that would appear no different had it been implemented
	// as a core feature of the C++ language itself.
	//
	// Use is simple:
	//
	//    unsigned int type_hash = clcpp::GetTypeNameHash<MyType>();
	//    const clcpp::Type* type = clcpp::GetType<MyType>();
	//
	// The type hash returned is independent of any loaded database, however, the type
	// pointer returned belongs to the database which was loaded by the module the call
	// resides in.
	//
	// ---------------------------------------------------------------------------------
	// IMPLEMENTATION DETAILS
	// ---------------------------------------------------------------------------------
	//
	// These functions are specified as no-inline so that they are embedded in your
	// module where clExport can pickup and store their addresses. These are recorded in
	// the exported database and are then inspected at runtime when the database is
	// loaded.
	//
	// Each type has its own implementation of the functions and thus the static variables
	// that they use. The database loader partially disassembles the function implementations,
	// finds the address of the variables that they use and patches them with whatever
	// values are stored in the database.
	//
	// When we patch GetType and GetTypeNameHash functions, we first search for
	// specific mov instructions, and when we find them, we would read the value
	// at the address calculated from the instruction. If the value equals the
	// identifier here, we would assume we find the location to patch.
	// This would require the following value will not be identical with any
	// other valid address used. That's why we use odd-ended values here, hoping
	// memory alignment will help us reduce the chance of being the same with
	// other addresses.
	//
   	#define CLCPP_INVALID_HASH (0xfefe012f)

    #if defined(CLCPP_USING_64_BIT)
	    #define CLCPP_INVALID_ADDRESS (0xffee01ef12349007)
    #else
    	#define CLCPP_INVALID_ADDRESS (0xffee6753)
    #endif // CLCPP_USING_64_BIT


	template <typename TYPE>
	CLCPP_NOINLINE unsigned int GetTypeNameHash()
	{
		static unsigned int hash = CLCPP_INVALID_HASH;
		return hash;
	}

	template <typename TYPE>
	CLCPP_NOINLINE const Type* GetType()
	{
		static const Type* type_ptr = (Type*)CLCPP_INVALID_ADDRESS;
		return type_ptr;
	}
}
