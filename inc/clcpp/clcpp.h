
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
#define crcpp_expand(x) x


//
// Join two symbols together, ensuring any macro arguments are evaluated before the join
//
#define crcpp_join2(x, y) x ## y
#define crcpp_join(x, y) crcpp_join2(x, y)


//
// Generate a unique symbol with the given prefix
//
#define crcpp_unique(x) crcpp_join(x, __COUNTER__)



//
// Export function signature
//
#define crcpp_export __declspec(dllexport)


#ifdef __clang__


	//
	// Injects a unique structure within the crcpp_internal namespace that only the Clang frontend
	// can see so that it can register the specified symbol for reflection.
	// Can only be called from the global namespace and results in the primitive and any children
	// being fully reflected.
	//
	#define crcpp_reflect(name)						\
													\
		namespace crcpp_internal					\
		{											\
			__attribute__((annotate("full-"#name)))	\
			struct crcpp_unique(crdb_reflect) { };	\
		}


	//
	// Similar to crcpp_reflect with the only difference being that the primitive being specified
	// is being partially reflected. Anything that is a child of that primitive has to be
	// explicitly reflected as a result.
	//
	#define crcpp_reflect_part(name)				\
													\
		namespace crcpp_internal					\
		{											\
			__attribute__((annotate("part-"#name)))	\
			struct crcpp_unique(crdb_reflect) { };	\
		}


	#define crcpp_attr(...) __attribute__((annotate("attr:" #__VA_ARGS__)))
	#define crcpp_push_attr(...) struct crcpp_unique(push_attr) { } __attribute__((annotate(#__VA_ARGS__)));
	#define crcpp_pop_attr(...) struct crcpp_unique(pop_attr) { } __attribute__((annotate(#__VA_ARGS__)));


	//
	// Clang does not need to see these
	//
	#define crcpp_get_type(db, type) ((const crcpp::Type*)0)
	#define crcpp_impl_class(scoped_type, type)


#else


	//
	// The main compiler does not need to see these
	//
	#define crcpp_reflect(name)
	#define crcpp_reflect_part(name)
	#define crcpp_attr(...)
	#define crcpp_push_attr(...)
	#define crcpp_pop_attr(...)


	namespace crcpp
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


	#define crcpp_get_type(db, type) crcpp::internal::GetType<type>(db)


	//
	// Introduces overloaded construction and destruction functions into the crcpp::internal
	// namespace for the type you specify. These functions end up in the list of method
	// in the specified type for easy access.
	// This can only be used from global namespace and you need to specify the type twice,
	// with and without scope.
	//
	#define crcpp_impl_class(scoped_type, type)							\
																		\
		namespace crcpp													\
		{																\
			namespace internal											\
			{															\
				crcpp_export void ConstructObject(scoped_type* object)	\
				{														\
					new (object) scoped_type;							\
				}														\
				crcpp_export void DestructObject(scoped_type* object)	\
				{														\
					((scoped_type*)object)->type::~type();				\
				}														\
			}															\
		}

#endif
