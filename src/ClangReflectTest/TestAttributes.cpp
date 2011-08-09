
#include <crcpp/crcpp.h>


crcpp_reflect(TestAttributes)
namespace TestAttributes
{
	// --------------------------------------------------------------------------------------------
	// Enum attributes
	crcpp_attr(enum_attr)
	enum GlobalEnumAttr { };

	// --------------------------------------------------------------------------------------------
	// Class attributes and those within its declaration
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

	// --------------------------------------------------------------------------------------------
	// Struct attributes and those within its declaration
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

	// --------------------------------------------------------------------------------------------
	// Function attributes can be on the implementation or declaration
	// If they're on both, they're merged
	crcpp_attr(function_attr_decl)
	void FunctionAttr();
	crcpp_attr(function_attr_def)
	void FunctionAttr() { }

	// --------------------------------------------------------------------------------------------
	// All types of attribute
	crcpp_attr(prop, val = 1, val2 = 1.5, val3 = symbol, val4 = "string")
	void AttrTypes() { }

	// --------------------------------------------------------------------------------------------
	// Test lexer/parser warnings
	crcpp_attr(error = 1.5.1)
	void FloatingPointLexError() { }
	crcpp_attr(error = $)
	void InvalidCharLexError() { }
	crcpp_attr(=)
	void SymbolExpectedParseError() { }
	crcpp_attr(error=)
	void EndOfAttrNoValueParseError() { }
	crcpp_attr(error=,valid)
	void MissingValueParserError() { }
}
