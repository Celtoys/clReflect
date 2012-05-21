
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
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
