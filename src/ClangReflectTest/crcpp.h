
// Generate a unique symbol with the given prefix
#define UNIQUE_SYMBOL2(x, y) x##y
#define UNIQUE_SYMBOL1(x, y) UNIQUE_SYMBOL2(x, y)
#define UNIQUE_SYMBOL(x) UNIQUE_SYMBOL1(x, __COUNTER__)


#ifdef __clang__

	#define crcpp_reflect(name)						\
													\
		namespace crdb_internal						\
		{											\
			__attribute__((annotate("full-"#name)))	\
			struct UNIQUE_SYMBOL(crdb_reflect) { };	\
		}

	#define crcpp_reflect_part(name)				\
													\
		namespace crdb_internal						\
		{											\
			__attribute__((annotate("part-"#name)))	\
			struct UNIQUE_SYMBOL(crdb_reflect) { };	\
		}


	#define crcpp_attr(...) __attribute__((annotate("attr:" #__VA_ARGS__)))
	#define crcpp_push_attr(...) struct UNIQUE_SYMBOL(push_attr) { } __attribute__((annotate(#__VA_ARGS__)));
	#define crcpp_pop_attr(...) struct UNIQUE_SYMBOL(pop_attr) { } __attribute__((annotate(#__VA_ARGS__)));

#endif
