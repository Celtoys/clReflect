
#include <crcpp/crcpp.h>


crcpp_reflect(TestAttributes)
namespace TestAttributes
{
	// --------------------------------------------------------------------------------------------
	// Enum attributes
	// NOTE the syntax difference that is specific to enums!
	enum crcpp_attr(enum_attr) GlobalEnumAttr { };

	// --------------------------------------------------------------------------------------------
	// Class attributes and those within its declaration
	crcpp_attr(class_attr)
	class ClassAttr
	{
		enum crcpp_attr(enum_attr) ClassEnumAttr { };

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
		enum crcpp_attr(enum_attr) ClassEnumAttr { };

		crcpp_attr(field_attr)
		int FieldAttr;

		crcpp_attr(method_attr)
		void MethodAttr() { }
	};

	// --------------------------------------------------------------------------------------------
	// Function attributes can be on the implementation or declaration
	// With function attributes, the declaration takes priority over the definition and the definition
	// attributes are discarded
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

void TestAttributesFunc(crcpp::Database& db)
{
	const crcpp::Enum* a = crcpp_get_type(db, TestAttributes::GlobalEnumAttr)->AsEnum();
	const crcpp::Class* b = crcpp_get_type(db, TestAttributes::ClassAttr)->AsClass();
	const crcpp::Enum* c = b->enums[0];
	const crcpp::Field* d = b->fields[0];
	const crcpp::Function* e = b->methods[0];
	const crcpp::Class* f = crcpp_get_type(db, TestAttributes::StructAttr)->AsClass();
	const crcpp::Enum* g = f->enums[0];
	const crcpp::Field* h = f->fields[0];
	const crcpp::Function* i = f->methods[0];

	const crcpp::Namespace* j = db.GetNamespace(db.GetName("TestAttributes").hash);
	const crcpp::Function* k = FindPrimitive(j->functions, db.GetName("TestAttributes::FunctionAttr").hash);
	const crcpp::Function* l = FindPrimitive(j->functions, db.GetName("TestAttributes::AttrTypes").hash);
}