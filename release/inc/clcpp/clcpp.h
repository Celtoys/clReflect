
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


	#define clcpp_attr(...) __attribute__((annotate("attr:" #__VA_ARGS__)))
	#define clcpp_push_attr(...) struct clcpp_unique(push_attr) { } __attribute__((annotate(#__VA_ARGS__)));
	#define clcpp_pop_attr(...) struct clcpp_unique(pop_attr) { } __attribute__((annotate(#__VA_ARGS__)));


	//
	// Clang does not need to see these
	//
	#define clcpp_get_type(db, type) ((const clcpp::Type*)0)
	#define clcpp_impl_class(scoped_type, type)


#else


	//
	// The main compiler does not need to see these
	//
	#define clcpp_reflect(name)
	#define clcpp_reflect_part(name)
	#define clcpp_attr(...)
	#define clcpp_push_attr(...)
	#define clcpp_pop_attr(...)


	namespace clcpp
	{
		namespace internal
		{
			template <typename TYPE>
			inline const Type* GetType(Database& db, const char* name)
			{
				// This is independent of the input database so can be cached per type
				// NOTE: Not thread-safe!
				static unsigned int hash = 0;

				if (hash == 0)
				{
					// Hash the name directly as we don't need to lookup the equivalent
					// in the names database
					hash = HashNameString(name);
					if (hash == 0)
					{
						return 0;
					}
				}

				return db.GetType(hash);
			}
		}
	}


	#define clcpp_get_type(db, type) clcpp::internal::GetType<type>(db, #type)


	//
	// Introduces overloaded construction and destruction functions into the clcpp::internal
	// namespace for the type you specify. These functions end up in the list of method
	// in the specified type for easy access.
	// This can only be used from global namespace and you need to specify the type twice,
	// with and without scope.
	//
	#define clcpp_impl_class(scoped_type, type)							\
																		\
		namespace clcpp													\
		{																\
			namespace internal											\
			{															\
				clcpp_export void ConstructObject(scoped_type* object)	\
				{														\
					new (PtrWrapper(object)) scoped_type;				\
				}														\
				clcpp_export void DestructObject(scoped_type* object)	\
				{														\
					((scoped_type*)object)->type::~type();				\
				}														\
			}															\
		}

#endif
