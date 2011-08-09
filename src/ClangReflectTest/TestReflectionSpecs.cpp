
// TODO: Reflecting a type from outside when the parent type or namespace hasn't been reflected

// Will a primitive be reflected?
//	Reflect a type from within a no-reflect namespace		NO
//	Reflect a type from within a no-reflect type			NO
//
// at each node, walk query upwards for the nearest reflection spec
// if found at this node, if it's full or partial is irrelevant - just reflect
// if found at a parent node, reflect if full, don't reflect if partial
// attributes override
//
// full->partial -- legal
// partial->full -- legal
// full->full -- illegal, already covered by the root
// <null>->any -- parents must have reflection specs
// illegal duplicate spec
// 

// Reflection specs assume you haven't got access to the type and can't modify its public API.
// This means you shouldn't be able to reflect from within types but also implies that you
// shouldn't be able to reflect from within the namespaces themselves. So... this all results
// in a simpler solution where you can only use reflection specs from the global namespace and
// always need to specify the fully scoped name.

// Always reflect members?

#include <crcpp/crcpp.h>


// --------------------------------------------------------------------------------------------
// Reflect all primitives in the global namespace with the specification coming before the definition
crcpp_reflect(NamespaceA)
crcpp_reflect(ClassA)
crcpp_reflect(EnumA)
crcpp_reflect(FunctionA)
namespace NamespaceA { class ShouldReflect { }; }
class ClassA { int ShouldReflect; };
enum EnumA { };
void FunctionA() { }


// --------------------------------------------------------------------------------------------
// Reflect all primitives in a namespace from outside before the definition
crcpp_reflect(NamespaceB::NamespaceA)
crcpp_reflect(NamespaceB::ClassA)
crcpp_reflect(NamespaceB::EnumA)
crcpp_reflect(NamespaceB::FunctionA)
namespace NamespaceB
{
	namespace NamespaceA { class ShouldReflect { }; }
	class ClassA { int ShouldReflect; };
	enum EnumA { };
	void FunctionA() { }
}


// --------------------------------------------------------------------------------------------
// Reflect all primitives in a nested namespace from global scope before the definition
crcpp_reflect(NamespaceD::Inner::NamespaceA)
crcpp_reflect(NamespaceD::Inner::ClassA)
crcpp_reflect(NamespaceD::Inner::EnumA)
crcpp_reflect(NamespaceD::Inner::FunctionA)
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
crcpp_reflect(NamespaceE::NamespaceA)
crcpp_reflect(NamespaceE::ClassA)
crcpp_reflect(NamespaceE::EnumA)
crcpp_reflect(NamespaceE::FunctionA)
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
crcpp_reflect(NamespaceF)
namespace NamespaceF
{
	namespace NamespaceA { class ShouldReflect { }; }
	class ClassA { int ShouldReflect; };
	enum EnumA { };
	void FunctionA() { }
}


// --------------------------------------------------------------------------------------------
// Trigger ill-formed Reflection Spec warnings
namespace crcpp_internal { }
namespace crcpp_internal { int x; }
namespace crcpp_internal { struct crdb_reflect_ { }; }


// --------------------------------------------------------------------------------------------
// Trigger duplicate spec warning
crcpp_reflect(NamespaceA)


// --------------------------------------------------------------------------------------------
// Trigger unnecessary reflection spec warnings
// TODO: See if specs map to actual symbols
crcpp_reflect(NamespaceG)
crcpp_reflect(NamespaceG::NamespaceA)
crcpp_reflect(NamespaceG::NamespaceA::C)
namespace G
{
	namespace A
	{
		class C { };
	}
}
