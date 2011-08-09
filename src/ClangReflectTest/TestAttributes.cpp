
#include <crcpp/crcpp.h>


crcpp_reflect(TestAttributes)
namespace TestAttributes
{
	crcpp_attr(enum_attr)
	enum GlobalEnumAttr { };

	crcpp_attr(class_attr)
	class ClassAttr
	{
		crcpp_attr(enum_attr)
		enum ClassEnumAttr { };

		crcpp_attr(field_attr)
		int FieldAttr;

		crcpp_attr(method_attr)
		void MethodAttr() { }
	};

	crcpp_attr(struct_attr)
	struct StructAttr
	{
		crcpp_attr(enum_attr)
		enum ClassEnumAttr { };

		crcpp_attr(field_attr)
		int FieldAttr;

		crcpp_attr(method_attr)
		void MethodAttr() { }
	};

	// Function attributes can be on the implementation or declaration
	// If they're on both, they're merged
	crcpp_attr(function_attr_decl)
	void FunctionAttr();
	crcpp_attr(function_attr_def)
	void FunctionAttr() { }


	crcpp_attr(prop, val = 1, val2 = 1.5, val3 = symbol, val4 = "string")
	void AttrTypes() { }
}
