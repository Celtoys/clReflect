
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
// Return a count of the number of arguments passed to a variadic macro, with an extra
// level of indirection to fix the variadic expansion in VC
//
#define crcpp_va_nargs_impl(_1, _2, _3, _4, _5, NARGS, ...) NARGS
#define crcpp_va_nargs(...) crcpp_expand(crcpp_va_nargs_impl(__VA_ARGS__, 5, 4, 3, 2, 1, 0))


//
// Join a macro parameter list of non-scoped symbols into one fully-scoped symbol
//
#define crcpp_make_symbol1(a) a
#define crcpp_make_symbol2(a, b) a::b
#define crcpp_make_symbol3(a, b, c) a::b::c
#define crcpp_make_symbol4(a, b, c, d) a::b::c::d
#define crcpp_make_symbol5(a, b, c, d, e) a::b::c::d::e


//
// Dispatch to the correct symbol maker depending upon number of arguments
//
#define crcpp_make_symbol(...) crcpp_join(crcpp_make_symbol, crcpp_va_nargs(__VA_ARGS__))(__VA_ARGS__)


//
// Join a macro parameter list of non-scoped symbols into a single symbol separated with underscores
//
#define crcpp_make_symbol_us_name1(a) a
#define crcpp_make_symbol_us_name2(a, b) a ## _ ## b
#define crcpp_make_symbol_us_name3(a, b, c) a ## _ ## b ## _ ## c
#define crcpp_make_symbol_us_name4(a, b, c, d) a ## _ ## b ## _ ## c ## _ ## d
#define crcpp_make_symbol_us_name5(a, b, c, d, e) a ## _ ## b ## _ ## c ## _ ## d ## _ ## e


//
// Dispatch to the correct underscore symbol maker depending upon number of arguments
//
#define crcpp_make_symbol_us_name(...) crcpp_join(crcpp_make_symbol_us_name, crcpp_va_nargs(__VA_ARGS__))(__VA_ARGS__)


#ifdef __clang__


	//
	// Injects a unique structure within the crcpp_internal namespace that only the Clang frontend
	// can see. This struct is annotated with the name of the primitive, which be passed to the
	// macro in comma-separated form. For example, to reflect a symbol of the name A::B::C, this
	// must be passed as crcpp_reflect(A, B, C).
	// Can only be called from the global namespace and results in the primitive and any children
	// being fully reflected.
	//
	#define crcpp_reflect(...)								\
															\
		namespace crcpp_internal							\
		{													\
			__attribute__((annotate("full-"#__VA_ARGS__)))	\
			struct crcpp_unique(crdb_reflect) { };			\
		}


	//
	// Similar to crcpp_reflect with the only difference being that the primitive being specified
	// is being partially reflected. Anything that is a child of that primitive has to be
	// explicitly reflected as a result.
	//
	#define crcpp_reflect_part(...)							\
															\
		namespace crcpp_internal							\
		{													\
			__attribute__((annotate("part-"#__VA_ARGS__)))	\
			struct crcpp_unique(crdb_reflect) { };			\
		}


	#define crcpp_attr(...) __attribute__((annotate("attr:" #__VA_ARGS__)))
	#define crcpp_push_attr(...) struct crcpp_unique(push_attr) { } __attribute__((annotate(#__VA_ARGS__)));
	#define crcpp_pop_attr(...) struct crcpp_unique(pop_attr) { } __attribute__((annotate(#__VA_ARGS__)));


#else


	namespace crcpp_internal
	{
		//
		// Select between two types depending upon the value of a boolean constant
		//
		template <typename bool, typename TRUE_TYPE, typename FALSE_TYPE> struct Select
		{
			typedef TRUE_TYPE Type;
		};
		template <typename TRUE_TYPE, typename FALSE_TYPE> struct Select<false, TRUE_TYPE, FALSE_TYPE>
		{
			typedef FALSE_TYPE Type;
		};


		//
		// An empty class for representing negative or invalid results
		//
		struct EmptyClass
		{
		};


		//
		// Returns the specified type if it's a class, otherwise returns the empty class
		// Uses the MS-specific extension __is_class, documented here: http://msdn.microsoft.com/en-us/library/ms177194(v=vs.80).aspx
		// Note that this is also GCC-compatible as this is how they achieve is_class in tr1\type_traits
		//
		template <typename TYPE> struct SelectIfClass
		{
			typedef typename Select<__is_class(TYPE), TYPE, EmptyClass>::Type Type;
		};
	}


	#define crcpp_reflect(...)																	\
																								\
		namespace crcpp_internal																\
		{																						\
			struct crcpp_make_symbol_us_name(crcpp_reflect, __VA_ARGS__)						\
				: public crcpp_internal::SelectIfClass<crcpp_make_symbol(__VA_ARGS__)>::Type	\
			{																					\
			};																					\
		}


	#define crcpp_reflect_part(...) crcpp_reflect(__VA_ARGS__)


#endif
