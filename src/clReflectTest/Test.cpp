

#include <clcpp/clcpp.h>


// test:
//	* inheritance
//	* enums
//	* references to other types
//	* overloaded functions
//	* unnamed function parameters
//	* method constness
//	* anonymous namespaces from different compilation units
//	* includes
//  offsets match between clang and msvc
//	defines introducing new stuff
//	bitfields
//	arrays
//	64-bit types
//	wchar_t
//	typedef
//	enum forward declaration
//
// not supported:
//	default parameters on functions
//	global variables
//	static class variables
//	multiple inheritance
//	virtual inheritance
//  unnamed enums
//	unnamed parameters in functions
//	anything in an anonymous namespace - the macros can't reference them
//  only one level of pointer/reference indirection (* supported, ** not)



// --------------------------------------------------------------------------------------------
// Named global enumeration
// --------------------------------------------------------------------------------------------
clcpp_reflect(NamedGlobalEnum)
enum NamedGlobalEnum
{
	VALUE_UNASSIGNED,
	VALUE_UNASSIGNED_PLUS_ONE,
	VALUE_ONE = 1,
	VALUE_THREE = 3,

	VALUE_64_BITS_UNSIGNED_MAX = (1LL << 64) - 1,
	VALUE_32_BITS_SIGNED_MAX = (1LL << 31) - 1,
	VALUE_32_BITS_SIGNED_MAX_PLUS_ONE = (1LL << 31),
	VALUE_32_BITS_UNSIGNED_MAX = (1LL << 32) - 1,
	VALUE_32_BITS_UNSIGNED_MAX_PLUS_ONE = (1LL << 32),
	VALUE_32_BITS_SIGNED_MIN = -(1LL << 31),
};


// --------------------------------------------------------------------------------------------
// Forward declaration and implementation of various global function types
// --------------------------------------------------------------------------------------------
void GlobalEmptyFunction();
void GlobalEmptyFunction() { }
int GlobalReturnFunction();
int GlobalReturnFunction() { return 0; }
void GlobalParamFunction(int pa, char pb);
void GlobalParamFunction(int pa, char pb) { }
char GlobalReturnParamFunction(float x, double y);
char GlobalReturnParamFunction(float x, double y) { return 0; }
clcpp_reflect(GlobalEmptyFunction)
clcpp_reflect(GlobalReturnFunction)
clcpp_reflect(GlobalParamFunction)
clcpp_reflect(GlobalReturnParamFunction)


// --------------------------------------------------------------------------------------------
// Global function overloads
// --------------------------------------------------------------------------------------------
void OverloadTest(int a) { }
void OverloadTest(int a, int b) { }
void OverloadTest(int a, int b, int c) { }
clcpp_reflect(OverloadTest)


// --------------------------------------------------------------------------------------------
// Forward declaration and definition of a global class
// --------------------------------------------------------------------------------------------
clcpp_reflect(ClassGlobalA)
class ClassGlobalA;
class ClassGlobalA
{
public:
	enum Enum { VALUE_A, VALUE_B };

	// Basic field types
	char x;
	short y;
	int z;

	// Declaration and implementation in separate locations
	void DeclEmptyFunction();
	int DeclReturnFunction();
	void DeclParamFunction(int pa, char pb);
	char DeclReturnParamFunction(float x, double y);

	// Methods with overload testing
	void OverloadTest(int a) { }
	void OverloadTest(int a, int b) { }
	void OverloadTest(int a, int b, int c) { }
};
void ClassGlobalA::DeclEmptyFunction() { }
int ClassGlobalA::DeclReturnFunction() { return 0; }
void ClassGlobalA::DeclParamFunction(int pa, char pb) { }
char ClassGlobalA::DeclReturnParamFunction(float x, double y) { return 0; }


// --------------------------------------------------------------------------------------------
// Forward declaration and definition of a global struct
// --------------------------------------------------------------------------------------------
clcpp_reflect(StructGlobalA)
struct StructGlobalA;
struct StructGlobalA
{
	StructGlobalA()
		: a(2)
	{
	}

	enum Enum { VALUE_A, VALUE_B };

	// Basic field types
	unsigned char a;
	unsigned short b;
	unsigned short c;

	// Inline implementation of varying function types
	void InlineEmptyFunction() { }
	int InlineReturnFunction() { return 0; }
	void InlineParamFunction(int pa, char pb) { }
	char InlineReturnParamFunction(float x, double y) { return 0; }

	// Methods with overload testing
	void OverloadTest(int a) { }
	void OverloadTest(int a, int b) { }
	void OverloadTest(int a, int b, int c) { }

	// Constness of the 'this' pointer
	char TestConstMethod(int a, float fb) const { return 0; }

	// Static with no 'this'
	static void StaticEmptyFunction() { }
	static int StaticRetFunction() { return 0; }
	static void StaticParamFunction(int a) { }
	static int StaticRetParamFunction(int a) { return a; }
};


// --------------------------------------------------------------------------------------------
// Inheritance relationships
// --------------------------------------------------------------------------------------------
clcpp_reflect(Inheritance)
namespace Inheritance
{
	struct BaseClass
	{
	};
	struct DerivedClass : public BaseClass
	{
	};
	struct ErrorClass
	{
	};
	// These two should kick up warnings
	struct MultipleInheritanceClass : public BaseClass, public ErrorClass
	{
	};
	struct VirtualInheritanceClass : virtual public BaseClass
	{
	};
}


// --------------------------------------------------------------------------------------------
// Varying field parameter types
// --------------------------------------------------------------------------------------------
clcpp_reflect(FieldTypes)
class FieldTypes
{
	// Just to keep the compiler happy about the reference types stored below
	FieldTypes();

	bool Bool;
	char Char;
	short Short;
	int Int;
	long Long;
	unsigned char UnsignedChar;
	unsigned short UnsignedShort;
	unsigned int UnsignedInt;
	unsigned long UnsignedLong;
	float Float;
	double Double;
	// --- Pointers
	bool* BoolPtr;
	char* CharPtr;
	short* ShortPtr;
	int* IntPtr;
	long* LongPtr;
	unsigned char* UnsignedCharPtr;
	unsigned short* UnsignedShortPtr;
	unsigned int* UnsignedIntPtr;
	unsigned long* UnsignedLongPtr;
	float* FloatPtr;
	double* DoublePtr;

	const bool* ConstBoolPtr;
	const char* ConstCharPtr;
	const short* ConstShortPtr;
	const int* ConstIntPtr;
	const long* ConstLongPtr;
	const unsigned char* ConstUnsignedCharPtr;
	const unsigned short* ConstUnsignedShortPtr;
	const unsigned int* ConstUnsignedIntPtr;
	const unsigned long* ConstUnsignedLongPtr;
	const float* ConstFloatPtr;
	const double* ConstDoublePtr;
	// --- References
	bool& BoolRef;
	char& CharRef;
	short& ShortRef;
	int& IntRef;
	long& LongRef;
	unsigned char& UnsignedCharRef;
	unsigned short& UnsignedShortRef;
	unsigned int& UnsignedIntRef;
	unsigned long& UnsignedLongRef;
	float& FloatRef;
	double& DoubleRef;

	const bool& ConstBoolRef;
	const char& ConstCharRef;
	const short& ConstShortRef;
	const int& ConstIntRef;
	const long& ConstLongRef;
	const unsigned char& ConstUnsignedCharRef;
	const unsigned short& ConstUnsignedShortRef;
	const unsigned int& ConstUnsignedIntRef;
	const unsigned long& ConstUnsignedLongRef;
	const float& ConstFloatRef;
	const double& ConstDoubleRef;
};


// --------------------------------------------------------------------------------------------
// Varying function parameter types
// --------------------------------------------------------------------------------------------
clcpp_reflect(FuncParams)
namespace FuncParams
{
	void FunctionTypes(char a, short b, int c, long d, unsigned char e, unsigned short f, unsigned int g, unsigned long h, float i, double j) { }
	void FunctionTypesPtr(char* a, short* b, int* c, long* d, unsigned char* e, unsigned short* f, unsigned int* g, unsigned long* h, float* i, double* j) { }
	void FunctionTypesConstPtr(const char* a, const short* b, const int* c, const long* d, const unsigned char* e, const unsigned short* f, const unsigned int* g, const unsigned long* h, const float* i, const double* j) { }
	void FunctionTypesRef(char& a, short& b, int& c, long& d, unsigned char& e, unsigned short& f, unsigned int& g, unsigned long& h, float& i, double& j) { }
	void FunctionTypesConstRef(const char& a, const short& b, const int& c, const long& d, const unsigned char& e, const unsigned short& f, const unsigned int& g, const unsigned long& h, const float& i, const double& j) { }
}


// --------------------------------------------------------------------------------------------
// Varying function return types
// --------------------------------------------------------------------------------------------
clcpp_reflect(FuncReturns)
namespace FuncReturns
{
	char FunctionRetChar() { return 0; }
	short FunctionRetShort() { return 0; }
	int FunctionRetInt() { return 0; }
	long FunctionRetLong() { return 0; }
	unsigned char FunctionRetUnsignedChar() { return 0; }
	unsigned short FunctionRetUnsignedShort() { return 0; }
	unsigned int FunctionRetUnsignedInt() { return 0; }
	unsigned long FunctionRetUnsignedLong() { return 0; }
	float FunctionRetFloat() { return 0; }
	double FunctionRetDouble() { return 0; }
	// --- Pointers
	char* FunctionRetCharPtr() { return 0; }
	short* FunctionRetShortPtr() { return 0; }
	int* FunctionRetIntPtr() { return 0; }
	long* FunctionRetLongPtr() { return 0; }
	unsigned char* FunctionRetUnsignedCharPtr() { return 0; }
	unsigned short* FunctionRetUnsignedShortPtr() { return 0; }
	unsigned int* FunctionRetUnsignedIntPtr() { return 0; }
	unsigned long* FunctionRetUnsignedLongPtr() { return 0; }
	float* FunctionRetFloatPtr() { return 0; }
	double* FunctionRetDoublePtr() { return 0; }

	const char* FunctionRetCharConstPtr() { return 0; }
	const short* FunctionRetShortConstPtr() { return 0; }
	const int* FunctionRetIntConstPtr() { return 0; }
	const long* FunctionRetLongConstPtr() { return 0; }
	const unsigned char* FunctionRetUnsignedCharConstPtr() { return 0; }
	const unsigned short* FunctionRetUnsignedShortConstPtr() { return 0; }
	const unsigned int* FunctionRetUnsignedIntConstPtr() { return 0; }
	const unsigned long* FunctionRetUnsignedLongConstPtr() { return 0; }
	const float* FunctionRetFloatConstPtr() { return 0; }
	const double* FunctionRetDoubleConstPtr() { return 0; }
	// --- References
	char& FunctionRetCharRef() { return *(char*)0; }
	short& FunctionRetShortRef() { return *(short*)0; }
	int& FunctionRetIntRef() { return *(int*)0; }
	long& FunctionRetLongRef() { return *(long*)0; }
	unsigned char& FunctionRetUnsignedCharRef() { return *(unsigned char*)0; }
	unsigned short& FunctionRetUnsignedShortRef() { return *(unsigned short*)0; }
	unsigned int& FunctionRetUnsignedIntRef() { return *(unsigned int*)0; }
	unsigned long& FunctionRetUnsignedLongRef() { return *(unsigned long*)0; }
	float& FunctionRetFloatRef() { return *(float*)0; }
	double& FunctionRetDoubleRef() { return *(double*)0; }

	const char& FunctionRetCharConstRef() { return *(char*)0; }
	const short& FunctionRetShortConstRef() { return *(short*)0; }
	const int& FunctionRetIntConstRef() { return *(int*)0; }
	const long& FunctionRetLongConstRef() { return *(long*)0; }
	const unsigned char& FunctionRetUnsignedCharConstRef() { return *(unsigned char*)0; }
	const unsigned short& FunctionRetUnsignedShortConstRef() { return *(unsigned short*)0; }
	const unsigned int& FunctionRetUnsignedIntConstRef() { return *(unsigned int*)0; }
	const unsigned long& FunctionRetUnsignedLongConstRef() { return *(unsigned long*)0; }
	const float& FunctionRetFloatConstRef() { return *(float*)0; }
	const double& FunctionRetDoubleConstRef() { return *(double*)0; }
}


// --------------------------------------------------------------------------------------------
// Named namespace
// --------------------------------------------------------------------------------------------
clcpp_reflect(NamespaceA)
namespace NamespaceA
{
	// Namespace functions with overload testing
	void OverloadTest(int a) { }
	void OverloadTest(int a, int b) { }
	void OverloadTest(int a, int b, int c) { }

	enum NamedNSEnumA { A_VALUE_A, A_VALUE_B };

	struct NamedNSClassA
	{
		enum EnumWithinNamedClassA { VALUE_A, VALUE_B };

		int FieldWithinNamedClassA;

		void FunctionWithinNamedClassA(int a) { }
	};
}


// --------------------------------------------------------------------------------------------
// Another named namespace
// --------------------------------------------------------------------------------------------
clcpp_reflect(NamespaceB)
namespace NamespaceB
{
	// Namespace functions with overload testing
	void OverloadTest(int a) { }
	void OverloadTest(int a, int b) { }
	void OverloadTest(int a, int b, int c) { }

	enum SecondNamedNSEnum { A_VALUE_A, A_VALUE_B };
	enum AnotherSecondNamedEnum { B_VALUE_A, B_VALUE_B };

	struct SecondNamedNSClass
	{
		enum ContainedEnum { VALUE_A, VALUE_B };

		int ContainedField;

		void ContainedFunction() { }
	};

	struct AnotherSecondNamedNSClass
	{
		enum AnotherContainedEnum { VALUE_A, VALUE_B };

		int AnotherContainedField;

		void AnotherContainedFunction() { }
	};
}


// --------------------------------------------------------------------------------------------
// Redeclaration of a named namespace
// --------------------------------------------------------------------------------------------
namespace NamespaceA
{
	enum NamedNSEnumB { B_VALUE_A, B_VALUE_B };

	struct NamedNSClassB
	{
		enum EnumWithinNamedClassB { VALUE_A, VALUE_B };

		int FieldWithinNamedClassB;

		void FunctionWithinNamedClassB() { }
	};
}


// --------------------------------------------------------------------------------------------
// Namespace and class nesting
// --------------------------------------------------------------------------------------------
clcpp_reflect(OuterNamespace)
namespace OuterNamespace
{
	namespace InnerNamespace
	{
		enum InnerNSEnum { VALUE_A, VALUE_B };

		struct OuterClass
		{
			enum OuterClassEnum { VALUE_A, VALUE_B };

			int OuterClassField;

			void OuterClassFunction() { }

			struct InnerClass
			{
				enum InnerClassEnum { VALUE_A, VALUE_B };

				int InnerClassField;

				void InnerClassFunction() { }
			};
		};
	}
}


// --------------------------------------------------------------------------------------------
// Referencing the already created classes as function parameters
// --------------------------------------------------------------------------------------------
clcpp_reflect(FunctionClasses)
void FunctionClasses(
					 ClassGlobalA a,
					 StructGlobalA b,
					 FieldTypes c,
					 NamespaceA::NamedNSClassA d,
					 NamespaceB::SecondNamedNSClass e,
					 NamespaceB::AnotherSecondNamedNSClass f,
					 NamespaceA::NamedNSClassB g,
					 OuterNamespace::InnerNamespace::OuterClass h,
					 OuterNamespace::InnerNamespace::OuterClass::InnerClass i) { }

clcpp_reflect(FunctionEnums)
void FunctionEnums(
				   NamedGlobalEnum a,
				   NamespaceA::NamedNSEnumA b,
				   NamespaceA::NamedNSClassA::EnumWithinNamedClassA  c,
				   NamespaceB::SecondNamedNSEnum d,
				   NamespaceB::AnotherSecondNamedEnum e,
				   NamespaceB::SecondNamedNSClass::ContainedEnum f,
				   NamespaceB::AnotherSecondNamedNSClass::AnotherContainedEnum g,
				   NamespaceA::NamedNSEnumB h,
				   NamespaceA::NamedNSClassB::EnumWithinNamedClassB i,
				   OuterNamespace::InnerNamespace::InnerNSEnum j,
				   OuterNamespace::InnerNamespace::OuterClass::OuterClassEnum k,
				   OuterNamespace::InnerNamespace::OuterClass::InnerClass::InnerClassEnum l) { }


// Trigger warnings for unnamed parameters
clcpp_reflect(myfunc)
int UnnamedParameterFunction(int)
{
	return 0;
}


// --------------------------------------------------------------------------------------------
// Trigger warnings for unreflected field types
// --------------------------------------------------------------------------------------------
struct MissingType
{
};
clcpp_reflect(TestMissingType)
namespace TestMissingType
{
	struct Struct
	{
		MissingType ThisShouldNotReflect;
		int y;
	};
	void Function(MissingType a);
	MissingType FunctionReturn();
	struct Inherit : public MissingType
	{
	};
}


void TestGetType(clcpp::Database& db)
{
	clcpp::Name na = db.GetName("ClassGlobalA");
	clcpp::Name nb = db.GetName("Inheritance::DerivedClass");
	clcpp::Name nc = db.GetName("NamespaceA::NamedNSClassA::EnumWithinNamedClassA");

	const clcpp::Type* e0 = clcpp_get_type(db, ClassGlobalA);
	const clcpp::Class* e1 = clcpp_get_type(db, Inheritance::DerivedClass)->AsClass();
	const clcpp::Enum* e2 = clcpp_get_type(db, NamespaceA::NamedNSClassA::EnumWithinNamedClassA)->AsEnum();
	int x;
}

