
#pragma once


// Placement new
#include <new>
// typeid calls for name()
#include <typeinfo>


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
			const Type* GetType(Database& db)
			{
				// These are independent of the input database so can be cached
				// NOTE: Not thread-safe!
				static const char* name = 0;
				static unsigned int hash = 0;

				if (name == 0)
				{
					// Strip Microsoft-specific type name prefixes
					name = typeid(TYPE).name();
					if (name[0] == 's' && name[6] == ' ')
					{
						name += sizeof("struct");
					}
					else if (name[0] == 'c' && name[5] == ' ')
					{
						name += sizeof("class");
					}
					else if (name[0] == 'e' && name[4] == ' ')
					{
						name += sizeof("enum");
					}

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


	#define clcpp_get_type(db, type) clcpp::internal::GetType<type>(db)


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
					new (object) scoped_type;							\
				}														\
				clcpp_export void DestructObject(scoped_type* object)	\
				{														\
					((scoped_type*)object)->type::~type();				\
				}														\
			}															\
		}

#endif
