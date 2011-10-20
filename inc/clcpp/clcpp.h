
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
#include "FunctionCall.h"


//
// Force an extra level of indirection for the preprocessor when expanding macros
//
#define clcpp_expand(x) x


//
// Join two symbols together, ensuring any macro arguments are evaluated before the join
//
#define clcpp_join2(x, y) x ## y
#define clcpp_join(x, y) clcpp_join2(x, y)


//
// Generate a unique symbol with the given prefix
//
#define clcpp_unique(x) clcpp_join(x, __COUNTER__)



//
// Export function signature
//
#define clcpp_export __declspec(dllexport)



#ifdef __clang__


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
			struct clcpp_unique(cldb_reflect) { };	\
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
			struct clcpp_unique(cldb_reflect) { };	\
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
			struct clcpp_unique(container_info) { };																\
		}


	#define clcpp_attr(...) __attribute__((annotate("attr:" #__VA_ARGS__)))
	#define clcpp_push_attr(...) struct clcpp_unique(push_attr) { } __attribute__((annotate(#__VA_ARGS__)));
	#define clcpp_pop_attr(...) struct clcpp_unique(pop_attr) { } __attribute__((annotate(#__VA_ARGS__)));


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


	namespace clcpp
	{
		namespace internal
		{
			//
			// Functions to abstract the calling of an object's constructor and destructor, for
			// debugging and letting the compiler do the type deduction.
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
		}
	}


	//
	// Introduces overloaded construction and destruction functions into the clcpp::internal
	// namespace for the type you specify. These functions end up in the list of methods
	// in the specified type for easy access.
	// This can only be used from global namespace.
	//
	#define clcpp_impl_class(type)								\
																\
		namespace clcpp											\
		{														\
			namespace internal									\
			{													\
				clcpp_export void ConstructObject(type* object)	\
				{												\
					CallConstructor(object);					\
				}												\
				clcpp_export void DestructObject(type* object)	\
				{												\
					CallDestructor(object);						\
				}												\
			}													\
		}


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
	// ----- IMPLEMENTATION DETAILS -----
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
	// The functions are specified as naked and implemented in inline asm so that the generated
	// code is predictable and doesn't vary between builds.
	//

	template <typename TYPE>
	__declspec(noinline) __declspec(naked) unsigned int GetTypeNameHash()
	{
		static unsigned int hash = 0;
		__asm
		{
			mov eax, dword ptr [hash]
			ret
		}
	}

	template <typename TYPE>
	__declspec(noinline) __declspec(naked) const Type* GetType()
	{
		static const Type* type_ptr = 0;
		__asm
		{
			mov eax, dword ptr [type_ptr]
			ret
		}
	}
}