
// test:
//	defines introducing new stuff
//	includes
//	bitfields
//	enums
//	* references to other types
//	64-bit types
//	wchar_t
//	* overloaded functions
//	* unnamed function parameters
//	* method constness
//	typedef
//	arrays
//	inheritance
//	anonymous namespaces from different compilation units
//	enum forward declaration
//
// not supported:
//	default parameters on functions
//	global variables
//	static class variables


// --------------------------------------------------------------------------------------------
// Named global enumeration
// --------------------------------------------------------------------------------------------
enum NamedGlobalEnum
{
	VALUE_UNASSIGNED,
	VALUE_UNASSIGNED_PLUS_ONE,
	VALUE_ONE = 1,
	VALUE_THREE = 3,

	VALUE_64_BITS_UNSIGNED_MAX = (1LL << 64) - 1,
	VALUE_32_BITS_SIGNED_MAX = (1LL << 31) - 1,
	VALUE_32_BITS_UNSIGNED_MAX = (1LL << 32) - 1,
	VALUE_32_BITS_UNSIGNED_MAX_PLUS_ONE = (1LL << 32),
	VALUE_32_BITS_SIGNED_MIN = -(1LL << 31),
};


// --------------------------------------------------------------------------------------------
// Unnamed global enumeration
// --------------------------------------------------------------------------------------------
enum
{
	UNNAMED_VALUE_UNASSIGNED,
	UNNAMED_VALUE_UNASSIGNED_PLUS_ONE,
	UNNAMED_VALUE_ONE = 1,
	UNNAMED_VALUE_THREE = 3,
	UNNAMED_VALUE_32BITS = 1 << 30,
	UNNAMED_VALUE_32BITS_TRAILING
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


// --------------------------------------------------------------------------------------------
// Global function overloads
// --------------------------------------------------------------------------------------------
void OverloadTest(int a) { }
void OverloadTest(int a, int b) { }
void OverloadTest(int a, int b, int c) { }


// --------------------------------------------------------------------------------------------
// Forward declaration and definition of a global class
// --------------------------------------------------------------------------------------------
class ClassGlobalA;
class ClassGlobalA
{
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
struct StructGlobalA;
struct StructGlobalA
{
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
class FieldTypes
{
	// Just to keep the compiler happy about the reference types stored below
	FieldTypes();

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
void FunctionTypes(char a, short b, int c, long d, unsigned char e, unsigned short f, unsigned int g, unsigned long h, float i, double j) { }
void FunctionTypesPtr(char* a, short* b, int* c, long* d, unsigned char* e, unsigned short* f, unsigned int* g, unsigned long* h, float* i, double* j) { }
void FunctionTypesConstPtr(const char* a, const short* b, const int* c, const long* d, const unsigned char* e, const unsigned short* f, const unsigned int* g, const unsigned long* h, const float* i, const double* j) { }
void FunctionTypesRef(char& a, short& b, int& c, long& d, unsigned char& e, unsigned short& f, unsigned int& g, unsigned long& h, float& i, double& j) { }
void FunctionTypesConstRef(const char& a, const short& b, const int& c, const long& d, const unsigned char& e, const unsigned short& f, const unsigned int& g, const unsigned long& h, const float& i, const double& j) { }


// --------------------------------------------------------------------------------------------
// Varying function return types
// --------------------------------------------------------------------------------------------
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


// --------------------------------------------------------------------------------------------
// Anonymous namespace
// --------------------------------------------------------------------------------------------
namespace
{
	enum AnonNSEnumA { A_VALUE_A, A_VALUE_B };

	class AnonNSClassA
	{
		enum EnumWithinAnonClassA { VALUE_A, VALUE_B };

		int FieldWithinAnonClassA;

		void FunctionWithinAnonClassA() { }
	};
}


// --------------------------------------------------------------------------------------------
// Anonymous namespace redeclared within the same compilation unit
// --------------------------------------------------------------------------------------------
namespace
{
	enum AnonNSEnumB { B_VALUE_A, B_VALUE_B };

	class AnonNSClassB
	{
		enum EnumWithinAnonClassB { VALUE_A, VALUE_B };

		int FieldWithinAnonClassB;

		void FunctionWithinAnonClassB() { }
	};
}


// --------------------------------------------------------------------------------------------
// Named namespace
// --------------------------------------------------------------------------------------------
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

		void FunctionWithinNamedClassA() { }
	};
}


// --------------------------------------------------------------------------------------------
// Another named namespace
// --------------------------------------------------------------------------------------------
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
void FunctionClasses(
					 ClassGlobalA a,
					 StructGlobalA b,
					 FieldTypes c,
					 AnonNSClassA d,
					 AnonNSClassB e,
					 NamespaceA::NamedNSClassA f,
					 NamespaceB::SecondNamedNSClass g,
					 NamespaceB::AnotherSecondNamedNSClass h,
					 NamespaceA::NamedNSClassB i,
					 OuterNamespace::InnerNamespace::OuterClass j,
					 OuterNamespace::InnerNamespace::OuterClass::InnerClass k) { }

void FunctionEnums(
				   NamedGlobalEnum a,
				   AnonNSEnumA b,
				   AnonNSEnumB c,
				   NamespaceA::NamedNSEnumA d,
				   NamespaceA::NamedNSClassA::EnumWithinNamedClassA  e,
				   NamespaceB::SecondNamedNSEnum f,
				   NamespaceB::AnotherSecondNamedEnum g,
				   NamespaceB::SecondNamedNSClass::ContainedEnum h,
				   NamespaceB::AnotherSecondNamedNSClass::AnotherContainedEnum i,
				   NamespaceA::NamedNSEnumB j,
				   NamespaceA::NamedNSClassB::EnumWithinNamedClassB k,
				   OuterNamespace::InnerNamespace::InnerNSEnum l,
				   OuterNamespace::InnerNamespace::OuterClass::OuterClassEnum m,
				   OuterNamespace::InnerNamespace::OuterClass::InnerClass::InnerClassEnum n) { }


