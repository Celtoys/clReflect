
//
// ===============================================================================
// clReflect, clcpp.h - Main runtime C++ API header file.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
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
// Indirection for stringify so that macro values work
//
#define CLCPP_STRINGIFY(...) #__VA_ARGS__

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

	#define CLCPP_CDECL __cdecl
	#define CLCPP_EXPORT __declspec(dllexport)

#elif defined(CLCPP_USING_GNUC)

	#define CLCPP_CDECL
	#define CLCPP_EXPORT

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
		typedef unsigned long long pointer_type;

		// type field used in printf/scanf
		#define CLCPP_SIZE_TYPE_HEX_FORMAT "lX"
		#define CLCPP_POINTER_TYPE_HEX_FORMAT "llX"

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
		struct PtrWrapper { };
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
			struct									\
			__attribute__((annotate("full-"#name)))	\
			CLCPP_UNIQUE(cldb_reflect) { };			\
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
			struct									\
			__attribute__((annotate("part-"#name)))	\
			CLCPP_UNIQUE(cldb_reflect) { };	\
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
			struct																									\
			__attribute__((annotate("container-" #container "-" #read_iterator "-" #write_iterator "-" #keyinfo)))	\
			CLCPP_UNIQUE(container_info) { };																\
		}

    #define clcpp_attr(...) __attribute__((annotate("attr:" CLCPP_STRINGIFY(__VA_ARGS__))))
    #define clcpp_push_attr(...) \
        struct CLCPP_UNIQUE(push_attr) \
        { \
        } __attribute__((annotate(CLCPP_STRINGIFY(__VA_ARGS__))));
    #define clcpp_pop_attr(...) \
        struct CLCPP_UNIQUE(pop_attr) \
        { \
        } __attribute__((annotate(CLCPP_STRINGIFY(__VA_ARGS__))));

	//
	// Clang does not need to see these
	//
	#define clcpp_impl_construct(type)
	#define clcpp_impl_destruct(type)
	#define clcpp_impl_class(type)


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
	// Introduces overloaded construction function into the clcpp::internal namespace for the type you
	// specify. This function ends up in the list of methods in the specified type for easy access.
	// This can only be used from global namespace.
	//
	#define clcpp_impl_construct(type)							\
		CLCPP_EXPORT void clcppConstructObject(type* object)	\
		{														\
			clcpp::internal::CallConstructor(object);			\
		}


	//
	// Introduces overloaded destruction function into the clcpp::internal namespace for the type you
	// specify. This function ends up in the list of methods in the specified type for easy access.
	// This can only be used from global namespace.
	//
	#define clcpp_impl_destruct(type)							\
		CLCPP_EXPORT void clcppDestructObject(type* object)		\
		{														\
			clcpp::internal::CallDestructor(object);			\
		}														\


	//
	// Introduces construction/destruction functions for the specified type.
	//
	#define clcpp_impl_class(type)	\
		clcpp_impl_construct(type)	\
		clcpp_impl_destruct(type)


#endif

// "transient" - These primitives are ignored during serialisation
#define attrFlag_Transient	0x00000001

// If an attribute starts with "load_" or "save_" then these flags are set to indicate there
// are custom loading functions assigned
#define attrFlag_CustomLoad 0x00000002
#define attrFlag_CustomSave 0x00000004

// Function to call before saving an object, specified with "pre_save" attribute
#define attrFlag_PreSave	0x00000008

// Function to call after loading an object, specified with "post_load" attribute
#define attrFlag_PostLoad	0x00000010



// ===============================================================================
//                Core Functionality Required by the Runtime C++ API
// ===============================================================================



clcpp_reflect_part(clcpp)
namespace clcpp
{
	namespace internal
	{
		//
		// Trivial assert
		//
		void Assert(bool expression);


		//
		// Functions to abstract the calling of an object's constructor and destructor, for
		// debugging and letting the compiler do the type deduction. Makes it a little easier
		// to use the PtrWrapper abstraction.
		//
		template <typename TYPE> inline void CallConstructor(TYPE* object)
		{
			new (*(PtrWrapper*)object) TYPE;
		}
		template <typename TYPE> inline void CallDestructor(TYPE* object)
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
	struct clcpp_attr(reflect_part) IAllocator
	{
		virtual void* Alloc(size_type size) = 0;
		virtual void Free(void* ptr) = 0;
	};


	//
	// Wrapper around a classic C-style array.
	// This is the client version that is stripped of all mutable functionality. It's designed to be
	// used as part of a memory map, supporting no C++ destructor logic.
	//
	template <typename TYPE> struct CArray
	{
		CArray() : size(0), data(0), allocator(0)
		{
		}

		TYPE& operator [] (unsigned int index)
		{
			internal::Assert(index < size);
			return data[index];
		}
		const TYPE& operator [] (unsigned int index) const
		{
			internal::Assert(index < size);
			return data[index];
		}

		unsigned int size;
		TYPE* data;
		IAllocator* allocator;
	};


	//
	// A simple file interface that the database loader will use. Clients must
	// implement this before they can load a reflection database.
	//
	struct clcpp_attr(reflect_part) IFile
	{
		// Derived classes must implement just the read function, returning
		// true on success, false otherwise.
		virtual bool Read(void* dest, size_type size) = 0;
	};


	//
	// Represents the range [start, end) for iterating over an array
	//
	struct Range
	{
		Range();
		unsigned int first;
		unsigned int last;
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
	struct clcpp_attr(reflect_part) Name
	{
		Name();
		bool operator == (const Name& rhs) const { return hash == rhs.hash; }
		unsigned int hash;
		const char* text;
	};


	//
	// Rather than create a new Type for "X" vs "const X", bloating the database,
	// this stores the qualifier separately. Additionally, the concept of whether
	// a type is a pointer, reference or not is folded in here as well.
	//
	struct clcpp_attr(reflect_part) Qualifier
	{
		enum Operator
		{
			VALUE,
			POINTER,
			REFERENCE
		};

		Qualifier();
		Qualifier(Operator op, bool is_const);

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
	struct clcpp_attr(reflect_part) ContainerInfo
	{
		enum
		{
			HAS_KEY = 1,
			IS_C_ARRAY = 2
		};

		ContainerInfo();

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
	struct clcpp_attr(reflect_part) Primitive
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

                Primitive() {}

                Primitive(Kind k);

		Kind kind;
		Name name;
		const Primitive* parent;

		// Database this primitive belongs to
		Database* database;
	};


	//
	// Base attribute type for collecting different attribute types together
	//
	struct clcpp_attr(reflect_part) Attribute : public Primitive
	{
		static const Kind KIND = KIND_ATTRIBUTE;

		Attribute();
		Attribute(Kind k);

		// Safe utility functions for casting to derived types
		const struct IntAttribute* AsIntAttribute() const;
		const struct FloatAttribute* AsFloatAttribute() const;
		const struct PrimitiveAttribute* AsPrimitiveAttribute() const;
		const struct TextAttribute* AsTextAttribute() const;
	};


	//
	// Representations of the different types of attribute available
	//
	// Flag attributes are always stored in an array of Attribute pointers. Checking
	// to see if an attribute is applied to a primitive involves searching the array
	// looking for the attribute by hash name.
	//
	// For flag attributes that are referenced so often that such a search becomes a
	// performance issue, they are also stored as bit flags in a 32-bit value.
	//
	//
	struct clcpp_attr(reflect_part) FlagAttribute : public Attribute
	{
		static const Kind KIND = KIND_FLAG_ATTRIBUTE;
		FlagAttribute() : Attribute(KIND) { }
	};
	struct clcpp_attr(reflect_part) IntAttribute : public Attribute
	{
		static const Kind KIND = KIND_INT_ATTRIBUTE;
		IntAttribute() : Attribute(KIND), value(0) { }
		int value;
	};
	struct clcpp_attr(reflect_part) FloatAttribute : public Attribute
	{
		static const Kind KIND = KIND_FLOAT_ATTRIBUTE;
		FloatAttribute() : Attribute(KIND), value(0) { }
		float value;
	};
	struct clcpp_attr(reflect_part) PrimitiveAttribute : public Attribute
	{
		static const Kind KIND = KIND_PRIMITIVE_ATTRIBUTE;
		PrimitiveAttribute() : Attribute(KIND), primitive(0) { }
		const Primitive* primitive;
	};
	struct clcpp_attr(reflect_part) TextAttribute : public Attribute
	{
		static const Kind KIND = KIND_TEXT_ATTRIBUTE;
		TextAttribute() : Attribute(KIND), value(0) { }
		const char* value;
	};


	//
	// A basic built-in type that classes/structs can also inherit from
	// Only one base type is supported until it becomes necessary to do otherwise.
	//
	struct clcpp_attr(reflect_part) Type : public Primitive
	{
		static const Kind KIND = KIND_TYPE;

		Type();
		Type(Kind k);

		// Does this type derive from the specified type, by hash?
		bool DerivesFrom(unsigned int type_name_hash) const;

		// Safe utility functions for casting to derived types
		const Enum* AsEnum() const;
		const TemplateType* AsTemplateType() const;
		const Class* AsClass() const;

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
	struct clcpp_attr(reflect_part) EnumConstant : public Primitive
	{
		static const Kind KIND = KIND_ENUM_CONSTANT;

		EnumConstant();

		int value;
	};


	//
	// A typed enumeration of name/value constant pairs
	//
	struct clcpp_attr(reflect_part) Enum : public Type
	{
		static const Kind KIND = KIND_ENUM;

		Enum();

		// All sorted by name
		CArray<const EnumConstant*> constants;
		CArray<const Attribute*> attributes;

		// Bits representing some of the flag attributes in the attribute array
		unsigned int flag_attributes;
	};


	//
	// Can be either a class/struct field or a function parameter
	//
	struct clcpp_attr(reflect_part) Field : public Primitive
	{
		static const Kind KIND = KIND_FIELD;

		Field();

		bool IsFunctionParameter() const;

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
	struct clcpp_attr(reflect_part) Function : public Primitive
	{
		static const Kind KIND = KIND_FUNCTION;

		Function();

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
	struct clcpp_attr(reflect_part) TemplateType : public Type
	{
		static const Kind KIND = KIND_TEMPLATE_TYPE;

		static const int MAX_NB_ARGS = 4;

		TemplateType();

		// A pointer to the type of each template argument
		const Type* parameter_types[MAX_NB_ARGS];

		// Specifies whether each argument is a pointer
		bool parameter_ptrs[MAX_NB_ARGS];
	};


	//
	// A template is not a type but a record of a template declaration without specified parameters
	// that instantiations can reference.
	//
	struct clcpp_attr(reflect_part) Template : public Primitive
	{
		static const Kind KIND = KIND_TEMPLATE;

		Template();

		// All sorted by name
		CArray<const TemplateType*> instances;
	};


	//
	// Description of a C++ struct or class with containing fields, functions, classes, etc.
	//
	struct clcpp_attr(reflect_part) Class : public Type
	{
		static const Kind KIND = KIND_CLASS;

		Class();

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
	struct clcpp_attr(reflect_part) Namespace : public Primitive
	{
		static const Kind KIND = KIND_NAMESPACE;

		Namespace();

		// All sorted by name
		CArray<const Namespace*> namespaces;
		CArray<const Type*> types;
		CArray<const Enum*> enums;
		CArray<const Class*> classes;
		CArray<const Function*> functions;
		CArray<const Template*> templates;
	};


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


	class clcpp_attr(reflect_part) Database
	{
	public:
		enum
		{
			// When a database is loaded, the function pointers stored within are rebased
			// using the load address of the calling module. Use this flag to disable
			// this behaviour.
			OPT_DONT_REBASE_FUNCTIONS = 0x00000001,
		};

		Database();
		~Database();

		bool Load(IFile* file, IAllocator* allocator, unsigned int options);
		bool Load(IFile* file, IAllocator* allocator, pointer_type base_address, unsigned int options);

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
};



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
	// THIS IMPLEMENTATION IS CURRENTLY IN DEVELOPMENT AND AWAITING FURTHER DOCUMENTATION
	//
	//
	template <typename TYPE> unsigned int GetTypeNameHash();
	template <typename TYPE> const Type* GetType();
}
