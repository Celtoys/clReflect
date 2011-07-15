
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


#ifdef __clang__

	#define crcpp_reflect(...)						\
													\
		namespace crcpp_internal					\
		{											\
			__attribute__((annotate("full-"#__VA_ARGS__)))	\
			struct crcpp_unique(crdb_reflect) { };	\
		}

	#define crcpp_reflect_part(...)					\
													\
		namespace crcpp_internal					\
		{											\
			__attribute__((annotate("part-"#__VA_ARGS__)))	\
			struct crcpp_unique(crdb_reflect) { };	\
		}


	#define crcpp_attr(...) __attribute__((annotate("attr:" #__VA_ARGS__)))
	#define crcpp_push_attr(...) struct crcpp_unique(push_attr) { } __attribute__((annotate(#__VA_ARGS__)));
	#define crcpp_pop_attr(...) struct crcpp_unique(pop_attr) { } __attribute__((annotate(#__VA_ARGS__)));

#endif
