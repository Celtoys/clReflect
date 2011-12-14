
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011 Don Williamson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ===============================================================================
//

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
	clcpp_attr(prop, val = 1, val2 = 1.5, val3 = symbol, val4 = "string", val5 = scoped::symbol)
	void AttrTypes() { }

	clcpp_attr(transient)
	void CommonFlagAttributes() { }

	// --------------------------------------------------------------------------------------------
	// Test lexer/parser warnings
	clcpp_attr(error = 1.5.1, load=FuncName)
	void FloatingPointLexError() { }
	clcpp_attr(error = $)
	void InvalidCharLexError() { }
	clcpp_attr(=)
	void SymbolExpectedParseError() { }
	clcpp_attr(error=)
	void EndOfAttrNoValueParseError() { }
	clcpp_attr(error=,valid)
	void MissingValueParserError() { }
	clcpp_attr(blah, noreflect)
	void NoReflectNotFirst() { }

	typedef const char* NullStr;
	struct Blah
	{
		const char* x;
	};
}

void TestAttributesFunc(clcpp::Database& db)
{
	const clcpp::Enum* a = clcpp::GetType<TestAttributes::GlobalEnumAttr>()->AsEnum();
	const clcpp::Class* b = clcpp::GetType<TestAttributes::ClassAttr>()->AsClass();
	const clcpp::Enum* c = b->enums[0];
	const clcpp::Field* d = b->fields[0];
	const clcpp::Function* e = b->methods[0];
	const clcpp::Class* f = clcpp::GetType<TestAttributes::StructAttr>()->AsClass();
	const clcpp::Enum* g = f->enums[0];
	const clcpp::Field* h = f->fields[0];
	const clcpp::Function* i = f->methods[0];

	const clcpp::Namespace* j = db.GetNamespace(db.GetName("TestAttributes").hash);
	const clcpp::Function* k = FindPrimitive(j->functions, db.GetName("TestAttributes::FunctionAttr").hash);
	const clcpp::Function* l = FindPrimitive(j->functions, db.GetName("TestAttributes::AttrTypes").hash);

	const clcpp::Function* common = FindPrimitive(j->functions, db.GetName("TestAttributes::CommonFlagAttributes").hash);

	const clcpp::Type* t = clcpp::GetType<TestAttributes::NullStr>();
}