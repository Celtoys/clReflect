
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


#endif
