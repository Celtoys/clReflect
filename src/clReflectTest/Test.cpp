
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clcpp/clcpp.h>
#include <cstdio>

// --------------------------------------------------------------------------------------------
// Enumerations
// --------------------------------------------------------------------------------------------
enum clcpp_attr(reflect) NamedGlobalEnum
{
	VALUE_UNASSIGNED,
	VALUE_UNASSIGNED_PLUS_ONE,
	VALUE_ONE = 1,
	VALUE_THREE = 3,

	VALUE_64_BITS_UNSIGNED_MAX = 4294967295,
	VALUE_32_BITS_SIGNED_MAX = (1LL << 31) - 1,
	VALUE_32_BITS_SIGNED_MAX_PLUS_ONE = (1LL << 31),
	VALUE_32_BITS_UNSIGNED_MAX = (1LL << 32) - 1,
	VALUE_32_BITS_UNSIGNED_MAX_PLUS_ONE = (1LL << 32),
	VALUE_32_BITS_SIGNED_MIN = -(1LL << 31),
};

enum class attrReflect ScopedEnum
{
    Value = 123
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
	wchar_t WChar;
	short Short;
	int Int;
	long Long;
	unsigned char UnsignedChar;
	unsigned short UnsignedShort;
	unsigned int UnsignedInt;
	unsigned long UnsignedLong;
	float Float;
	double Double;
	clcpp::int64 Int64;
	clcpp::uint64 UnsignedInt64;
	// --- Pointers
	bool* BoolPtr;
	char* CharPtr;
	wchar_t* WCharPtr;
	short* ShortPtr;
	int* IntPtr;
	long* LongPtr;
	unsigned char* UnsignedCharPtr;
	unsigned short* UnsignedShortPtr;
	unsigned int* UnsignedIntPtr;
	unsigned long* UnsignedLongPtr;
	float* FloatPtr;
	double* DoublePtr;
	clcpp::int64* Int64Ptr;
	clcpp::uint64* UnsignedInt64Ptr;

	const bool* ConstBoolPtr;
	const char* ConstCharPtr;
	const wchar_t* ConstWCharPtr;
	const short* ConstShortPtr;
	const int* ConstIntPtr;
	const long* ConstLongPtr;
	const unsigned char* ConstUnsignedCharPtr;
	const unsigned short* ConstUnsignedShortPtr;
	const unsigned int* ConstUnsignedIntPtr;
	const unsigned long* ConstUnsignedLongPtr;
	const float* ConstFloatPtr;
	const double* ConstDoublePtr;
	const clcpp::int64* ConstInt64Ptr;
	const clcpp::uint64* ConstUnsignedInt64Ptr;
	// --- References
	bool& BoolRef;
	char& CharRef;
	wchar_t& WCharRef;
	short& ShortRef;
	int& IntRef;
	long& LongRef;
	unsigned char& UnsignedCharRef;
	unsigned short& UnsignedShortRef;
	unsigned int& UnsignedIntRef;
	unsigned long& UnsignedLongRef;
	float& FloatRef;
	double& DoubleRef;
	clcpp::int64& Int64Ref;
	clcpp::uint64& UnsignedInt64Ref;

	const bool& ConstBoolRef;
	const char& ConstCharRef;
	const wchar_t& ConstWCharRef;
	const short& ConstShortRef;
	const int& ConstIntRef;
	const long& ConstLongRef;
	const unsigned char& ConstUnsignedCharRef;
	const unsigned short& ConstUnsignedShortRef;
	const unsigned int& ConstUnsignedIntRef;
	const unsigned long& ConstUnsignedLongRef;
	const float& ConstFloatRef;
	const double& ConstDoubleRef;
	const clcpp::int64& ConstInt64Ref;
	const clcpp::uint64& ConstUnsignedInt64Ref;
};


// --------------------------------------------------------------------------------------------
// Varying function parameter types
// --------------------------------------------------------------------------------------------
clcpp_reflect(FuncParams)
namespace FuncParams
{
	void FunctionTypes(
		bool a,
		char b,
		wchar_t c,
		short d,
		int e,
		long f,
		unsigned char g,
		unsigned short h,
		unsigned int i,
		unsigned long j,
		float k,
		double l,
		clcpp::int64 m,
		clcpp::uint64 n) { }
	void FunctionTypesPtr(
		bool* a,
		char* b,
		wchar_t* c,
		short* d,
		int* e,
		long* f,
		unsigned char* g,
		unsigned short* h,
		unsigned int* i,
		unsigned long* j,
		float* k,
		double* l,
		clcpp::int64* m,
		clcpp::uint64* n) { }
	void FunctionTypesConstPtr(
		const bool* a,
		const char* b,
		const wchar_t* c,
		const short* d,
		const int* e,
		const long* f,
		const unsigned char* g,
		const unsigned short* h,
		const unsigned int* i,
		const unsigned long* j,
		const float* k,
		const double* l,
		const clcpp::int64* m,
		const clcpp::uint64* n) { }
	void FunctionTypesRef(
		bool& a,
		char& b,
		wchar_t& c,
		short& d,
		int& e,
		long& f,
		unsigned char& g,
		unsigned short& h,
		unsigned int& i,
		unsigned long& j,
		float& k,
		double& l,
		clcpp::int64& m,
		clcpp::uint64& n) { }
	void FunctionTypesConstRef(
		const bool& a,
		const char& b,
		const wchar_t& c,
		const short& d,
		const int& e,
		const long& f,
		const unsigned char& g,
		const unsigned short& h,
		const unsigned int& i,
		const unsigned long& j,
		const float& k,
		const double& l,
		const clcpp::int64& m,
		const clcpp::uint64& n) { }
}


// --------------------------------------------------------------------------------------------
// Varying function return types
// --------------------------------------------------------------------------------------------
clcpp_reflect(FuncReturns)
namespace FuncReturns
{
	bool FunctionRetBool() { return 0; }
	char FunctionRetChar() { return 0; }
	wchar_t FunctionRetWChar() { return 0; }
	short FunctionRetShort() { return 0; }
	int FunctionRetInt() { return 0; }
	long FunctionRetLong() { return 0; }
	unsigned char FunctionRetUnsignedChar() { return 0; }
	unsigned short FunctionRetUnsignedShort() { return 0; }
	unsigned int FunctionRetUnsignedInt() { return 0; }
	unsigned long FunctionRetUnsignedLong() { return 0; }
	float FunctionRetFloat() { return 0; }
	double FunctionRetDouble() { return 0; }
	clcpp::int64 FunctionRetInt64() { return 0; }
	clcpp::uint64 FunctionRetUnsignedInt64() { return 0; }

	// --- Pointers
	bool* FunctionRetBoolPtr() { return 0; }
	char* FunctionRetCharPtr() { return 0; }
	wchar_t* FunctionRetWCharPtr() { return 0; }
	short* FunctionRetShortPtr() { return 0; }
	int* FunctionRetIntPtr() { return 0; }
	long* FunctionRetLongPtr() { return 0; }
	unsigned char* FunctionRetUnsignedCharPtr() { return 0; }
	unsigned short* FunctionRetUnsignedShortPtr() { return 0; }
	unsigned int* FunctionRetUnsignedIntPtr() { return 0; }
	unsigned long* FunctionRetUnsignedLongPtr() { return 0; }
	float* FunctionRetFloatPtr() { return 0; }
	double* FunctionRetDoublePtr() { return 0; }
	clcpp::int64* FunctionRetInt64Ptr() { return 0; }
	clcpp::uint64* FunctionRetUnsignedInt64Ptr() { return 0; }

	// --- Const Pointers
	const bool* FunctionRetBoolConstPtr() { return 0; }
	const char* FunctionRetCharConstPtr() { return 0; }
	const wchar_t* FunctionRetWCharConstPtr() { return 0; }
	const short* FunctionRetShortConstPtr() { return 0; }
	const int* FunctionRetIntConstPtr() { return 0; }
	const long* FunctionRetLongConstPtr() { return 0; }
	const unsigned char* FunctionRetUnsignedCharConstPtr() { return 0; }
	const unsigned short* FunctionRetUnsignedShortConstPtr() { return 0; }
	const unsigned int* FunctionRetUnsignedIntConstPtr() { return 0; }
	const unsigned long* FunctionRetUnsignedLongConstPtr() { return 0; }
	const float* FunctionRetFloatConstPtr() { return 0; }
	const double* FunctionRetDoubleConstPtr() { return 0; }
	const clcpp::int64* FunctionRetConstInt64Ptr() { return 0; }
	const clcpp::uint64* FunctionRetConstUnsignedInt64Ptr() { return 0; }

	// --- References
    bool& FunctionRetBoolRef()
    {
        static bool v;
        return v;
    }
    char& FunctionRetCharRef()
    {
        static char v;
        return v;
    }
    wchar_t& FunctionRetWCharRef()
    {
        static wchar_t v;
        return v;
    }
    short& FunctionRetShortRef()
    {
        static short v;
        return v;
    }
    int& FunctionRetIntRef()
    {
        static int v;
        return v;
    }
    long& FunctionRetLongRef()
    {
        static long v;
        return v;
    }
    unsigned char& FunctionRetUnsignedCharRef()
    {
        static unsigned char v;
        return v;
    }
    unsigned short& FunctionRetUnsignedShortRef()
    {
        static unsigned short v;
        return v;
    }
    unsigned int& FunctionRetUnsignedIntRef()
    {
        static unsigned int v;
        return v;
    }
    unsigned long& FunctionRetUnsignedLongRef()
    {
        static unsigned long v;
        return v;
    }
    float& FunctionRetFloatRef()
    {
        static float v;
        return v;
    }
    double& FunctionRetDoubleRef()
    {
        static double v;
        return v;
    }
    clcpp::int64& FunctionRetInt64Ref()
    {
        static clcpp::int64 v;
        return v;
    }
    clcpp::uint64& FunctionRetUnsignedInt64Ref()
    {
        static clcpp::uint64 v;
        return v;
    }

    // --- Const References
    const bool& FunctionRetBoolConstRef()
    {
        static bool v;
        return v;
    }
    const char& FunctionRetCharConstRef()
    {
        static char v;
        return v;
    }
    const wchar_t& FunctionRetWCharConstRef()
    {
        static wchar_t v;
        return v;
    }
    const short& FunctionRetShortConstRef()
    {
        static short v;
        return v;
    }
    const int& FunctionRetIntConstRef()
    {
        static int v;
        return v;
    }
    const long& FunctionRetLongConstRef()
    {
        static long v;
        return v;
    }
    const unsigned char& FunctionRetUnsignedCharConstRef()
    {
        static unsigned char v;
        return v;
    }
    const unsigned short& FunctionRetUnsignedShortConstRef()
    {
        static unsigned short v;
        return v;
    }
    const unsigned int& FunctionRetUnsignedIntConstRef()
    {
        static unsigned int v;
        return v;
    }
    const unsigned long& FunctionRetUnsignedLongConstRef()
    {
        static unsigned long v;
        return v;
    }
    const float& FunctionRetFloatConstRef()
    {
        static float v;
        return v;
    }
    const double& FunctionRetDoubleConstRef()
    {
        static double v;
        return v;
    }
    const clcpp::int64& FunctionRetConstInt64Ref()
    {
        static clcpp::int64 v;
        return v;
    }
    const clcpp::uint64& FunctionRetConstUnsignedInt64Ref()
    {
        static clcpp::uint64 v;
        return v;
    }
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
clcpp_reflect(UnnamedParameterFunction)
int UnnamedParameterFunction(int)
{
	return 0;
}

struct clcpp_attr(reflect) Blah2
{
};


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

	const clcpp::Type* e0 = clcpp::GetType<ClassGlobalA>();
	const clcpp::Class* e1 = clcpp::GetType<Inheritance::DerivedClass>()->AsClass();

	const clcpp::Type* b = clcpp::GetType<Blah2>();

	printf("%x\n", clcpp::GetTypeNameHash<int>());
	printf("%x\n", clcpp::GetTypeNameHash<unsigned int>());
	printf("%x\n", clcpp::GetTypeNameHash<char>());
	printf("%x\n", clcpp::GetTypeNameHash<ClassGlobalA>());
	printf("%x\n", clcpp::GetTypeNameHash<Inheritance::DerivedClass>());
}

