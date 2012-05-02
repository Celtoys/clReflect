
//
// ===============================================================================
// clReflect, clcpp.h - Main runtime C++ API header file. Includes all others and
// defines many of the API macros.
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


// Include all dependencies
// These can be included independently if you want quicker compiles
#include "Core.h"
#include "Database.h"


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
