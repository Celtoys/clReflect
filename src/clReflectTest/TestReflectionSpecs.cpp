
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


// --------------------------------------------------------------------------------------------
// Reflect all primitives in the global namespace with the specification coming before the definition
clcpp_reflect(NamespaceA)
clcpp_reflect(ClassA)
clcpp_reflect(EnumA)
clcpp_reflect(FunctionA)
namespace NamespaceA { class ShouldReflect { }; }
class ClassA { int ShouldReflect; };
enum EnumA { };
void FunctionA() { }


// --------------------------------------------------------------------------------------------
// Reflect all primitives in a namespace from outside before the definition
clcpp_reflect(NamespaceB::NamespaceA)
clcpp_reflect(NamespaceB::ClassA)
clcpp_reflect(NamespaceB::EnumA)
clcpp_reflect(NamespaceB::FunctionA)
namespace NamespaceB
{
	namespace NamespaceA { class ShouldReflect { }; }
	class ClassA { int ShouldReflect; };
	enum EnumA { };
	void FunctionA() { }
}


// --------------------------------------------------------------------------------------------
// Reflect all primitives in a nested namespace from global scope before the definition
clcpp_reflect(NamespaceD::Inner::NamespaceA)
clcpp_reflect(NamespaceD::Inner::ClassA)
clcpp_reflect(NamespaceD::Inner::EnumA)
clcpp_reflect(NamespaceD::Inner::FunctionA)
namespace NamespaceD
{
	namespace Inner
	{
		namespace NamespaceA { class ShouldReflect { }; }
		class ClassA { int ShouldReflect; };
		enum EnumA { };
		void FunctionA() { }
	}
}


// --------------------------------------------------------------------------------------------
// Partial reflect a namespace with only half the contents reflected
clcpp_reflect(NamespaceE::NamespaceA)
clcpp_reflect(NamespaceE::ClassA)
clcpp_reflect(NamespaceE::EnumA)
clcpp_reflect(NamespaceE::FunctionA)
namespace NamespaceE
{
	namespace NamespaceA { class ShouldReflect { }; }
	class ClassA { int ShouldReflect; };
	enum EnumA { };
	void FunctionA() { }

	namespace ShouldNotReflectA { }
	class ShouldNotReflectB { };
	enum ShouldNotReflectC { };
	void ShouldNotReflectD() { }
}


// --------------------------------------------------------------------------------------------
// Full reflect of the contents of the namespace
clcpp_reflect(NamespaceF)
namespace NamespaceF
{
	namespace NamespaceA { class ShouldReflect { }; }
	class ClassA { int ShouldReflect; };
	enum EnumA { };
	void FunctionA() { }
}


// --------------------------------------------------------------------------------------------
// Trigger ill-formed Reflection Spec warnings
namespace clcpp_internal { }
namespace clcpp_internal { int x; }
namespace clcpp_internal { struct cldb_reflect_ { }; }


// --------------------------------------------------------------------------------------------
// Trigger duplicate spec warning
clcpp_reflect(NamespaceA)


// --------------------------------------------------------------------------------------------
// Trigger unnecessary reflection spec warnings
// TODO: See if specs map to actual symbols
clcpp_reflect(NamespaceG)
clcpp_reflect(NamespaceG::NamespaceA)
clcpp_reflect(NamespaceG::NamespaceA::C)
namespace G
{
	namespace A
	{
		class C { };
	}
}
