
#include <clcpp/clcpp.h>


clcpp_reflect(TestAttributes)
namespace TestAttributes
{
	// --------------------------------------------------------------------------------------------
	// Enum attributes
	// NOTE the syntax difference that is specific to enums!
	enum clcpp_attr(enum_attr) GlobalEnumAttr { };

	// --------------------------------------------------------------------------------------------
	// Class attributes and those within its declaration
	clcpp_attr(class_attr)
	class ClassAttr
	{
		enum clcpp_attr(enum_attr) ClassEnumAttr { };

		clcpp_attr(field_attr)
		int FieldAttr;

		clcpp_attr(method_attr)
		void MethodAttr() { }
	};

	// --------------------------------------------------------------------------------------------
	// Struct attributes and those within its declaration
	clcpp_attr(struct_attr)
	struct StructAttr
	{
		enum clcpp_attr(enum_attr) ClassEnumAttr { };

		clcpp_attr(field_attr)
		int FieldAttr;

		clcpp_attr(method_attr)
		void MethodAttr() { }
	};

	// --------------------------------------------------------------------------------------------
	// Function attributes can be on the implementation or declaration
	// With function attributes, the declaration takes priority over the definition and the definition
	// attributes are discarded
	clcpp_attr(function_attr_decl)
	void FunctionAttr();
	clcpp_attr(function_attr_def)
	void FunctionAttr() { }

	// --------------------------------------------------------------------------------------------
	// All types of attribute
	clcpp_attr(prop, val = 1, val2 = 1.5, val3 = symbol, val4 = "string")
	void AttrTypes() { }

	// --------------------------------------------------------------------------------------------
	// Test lexer/parser warnings
	clcpp_attr(error = 1.5.1)
	void FloatingPointLexError() { }
	clcpp_attr(error = $)
	void InvalidCharLexError() { }
	clcpp_attr(=)
	void SymbolExpectedParseError() { }
	clcpp_attr(error=)
	void EndOfAttrNoValueParseError() { }
	clcpp_attr(error=,valid)
	void MissingValueParserError() { }
}

void TestAttributesFunc(clcpp::Database& db)
{
	const clcpp::Enum* a = clcpp_get_type(db, TestAttributes::GlobalEnumAttr)->AsEnum();
	const clcpp::Class* b = clcpp_get_type(db, TestAttributes::ClassAttr)->AsClass();
	const clcpp::Enum* c = b->enums[0];
	const clcpp::Field* d = b->fields[0];
	const clcpp::Function* e = b->methods[0];
	const clcpp::Class* f = clcpp_get_type(db, TestAttributes::StructAttr)->AsClass();
	const clcpp::Enum* g = f->enums[0];
	const clcpp::Field* h = f->fields[0];
	const clcpp::Function* i = f->methods[0];

	const clcpp::Namespace* j = db.GetNamespace(db.GetName("TestAttributes").hash);
	const clcpp::Function* k = FindPrimitive(j->functions, db.GetName("TestAttributes::FunctionAttr").hash);
	const clcpp::Function* l = FindPrimitive(j->functions, db.GetName("TestAttributes::AttrTypes").hash);
}