
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clcpp/clcpp.h>
#include <string.h>
#include <stdio.h>


#define EXPAND(x) x
#define VA_NARGS_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, NARGS, ...) NARGS
#define VA_NARGS(...) EXPAND(VA_NARGS_IMPL(__VA_ARGS__, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))

#define BEGIN_TEST(name)									\
		typedef name TestType;								\
		clcpp_attr(noreflect)								\
		static Tester<name>& GetTester(clcpp::Database& db)	\
		{													\
			static Tester<name> t(db, "Offsets::" #name);	\
			return t;										\
		}													\
		clcpp_attr(noreflect)								\
		static void Test(clcpp::Database& db)

#define TEST(name) GetTester(db).Test(#name, &TestType::name)

#define TESTS_1(a, ...)		TEST(a);
#define TESTS_2(a, ...)		TEST(a); EXPAND(TESTS_1(__VA_ARGS__))
#define TESTS_3(a, ...)		TEST(a); EXPAND(TESTS_2(__VA_ARGS__))
#define TESTS_4(a, ...)		TEST(a); EXPAND(TESTS_3(__VA_ARGS__));
#define TESTS_5(a, ...)		TEST(a); EXPAND(TESTS_4(__VA_ARGS__));
#define TESTS_6(a, ...)		TEST(a); EXPAND(TESTS_5(__VA_ARGS__));
#define TESTS_7(a, ...)		TEST(a); EXPAND(TESTS_6(__VA_ARGS__));
#define TESTS_8(a, ...)		TEST(a); EXPAND(TESTS_7(__VA_ARGS__));
#define TESTS_9(a, ...)		TEST(a); EXPAND(TESTS_8(__VA_ARGS__));
#define TESTS_10(a, ...)	TEST(a); EXPAND(TESTS_9(__VA_ARGS__));
#define TESTS_11(a, ...)	TEST(a); EXPAND(TESTS_10(__VA_ARGS__));
#define TESTS_12(a, ...)	TEST(a); EXPAND(TESTS_11(__VA_ARGS__));
#define TESTS_13(a, ...)	TEST(a); EXPAND(TESTS_12(__VA_ARGS__));
#define TESTS_14(a, ...)	TEST(a); EXPAND(TESTS_13(__VA_ARGS__));
#define TESTS_15(a, ...)	TEST(a); EXPAND(TESTS_14(__VA_ARGS__));
#define TESTS_16(a, ...)	TEST(a); EXPAND(TESTS_15(__VA_ARGS__));
#define TESTS_17(a, ...)	TEST(a); EXPAND(TESTS_16(__VA_ARGS__));
#define TESTS_18(a, ...)	TEST(a); EXPAND(TESTS_17(__VA_ARGS__));
#define TESTS_19(a, ...)	TEST(a); EXPAND(TESTS_18(__VA_ARGS__));
#define TESTS_20(a, ...)	TEST(a); EXPAND(TESTS_19(__VA_ARGS__));
#define TESTS(name, ...)	BEGIN_TEST(name) { EXPAND(CLCPP_JOIN(TESTS_, VA_NARGS(__VA_ARGS__))(__VA_ARGS__)) }


// Different from the classic offsetoff macro in that it produces reproduceable behaviour per platform/compiler
// This allows me to measure offsets with virtual inheritance without the compiler crashing!
template <typename CLASS_TYPE, typename FIELD_TYPE>
int offsetof(FIELD_TYPE (CLASS_TYPE::*field))
{
	CLASS_TYPE object;
	return (char*)&(object.*field) - (char*)&object;
}


template <typename TYPE>
struct Tester
{
	Tester(clcpp::Database& db, const char* class_name)
		: m_DB(db)
		, m_ClassName(class_name)
		, m_Class(0)
	{
		const clcpp::Type* type = db.GetType(db.GetName(m_ClassName).hash);
		if (type)
		{
			m_Class = type->AsClass();
			int native_size = sizeof(TYPE);
			int clang_size = m_Class->size;
			printf("\n%-40s %5d %5d %s\n", m_ClassName, native_size, clang_size, native_size != clang_size ? "FAILED" : "");
		}
		else
		{
			int native_size = sizeof(TYPE);
			printf("\n%-40s %5d %5d %s\n", m_ClassName, native_size, -1, "CLASS NOT REGISTERED");
		}
		printf("----------------------------------------\n");
	}

	template <typename CLASS_TYPE, typename FIELD_TYPE>
	void Test(const char* field_name, FIELD_TYPE (CLASS_TYPE::*field_data))
	{
		//int native_offset = (int)&(((CLASS_TYPE*)0)->*field_data);
		int native_offset = offsetof(field_data);
		if (m_Class)
		{
			// Compare offsets and print the results
			const clcpp::Field* field = clcpp::FindPrimitive(m_Class->fields, m_DB.GetName(field_name).hash);
			if (field)
			{
				int clang_offset = field->offset;
				printf("%-40s %5d %5d %s\n", field_name, native_offset, clang_offset, native_offset != clang_offset ? "FAILED" : "");
			}
		}
		else
		{
			printf("%-40s %5d\n", field_name, native_offset);
		}
	}

	const clcpp::Database& m_DB;
	const char* m_ClassName;
	const clcpp::Class* m_Class;
};


clcpp_reflect(Offsets)
namespace Offsets
{
	// Virtual function table pointer should be at the beginning
	struct PolymorphicStruct
	{
		virtual void Empty() { }
		int a;
		char b;
		short c;
		float d;
		TESTS(PolymorphicStruct, a, b, c, d);
	};

	// No change in behaviour from the base should be observed
	struct DerivedPolymorphicStruct : public PolymorphicStruct
	{
		virtual void Empty() { }
		char e;
		int f;
		short g;
		float h;
		TESTS(DerivedPolymorphicStruct, e, f, g, h);
	};

	// This is an example of what the runtime API currently can't handle because fields are stored per-class
	// and each class has a "base class" pointer. However, the layouts should still be calculated correctly.
	struct PODBase
	{
		int a;
		int b;
		int c;
	};
	struct DerivedPolymorphicWithPODBase : public PODBase
	{
		virtual void Empty() { }
		TESTS(PODBase, a, b, c);
	};

	// The addition of a double anywhere in this struct forces the vtable ptr to occupy 4 bytes + 4 bytes padding in MSVC
	struct DoubleInPolymorphicStruct
	{
		virtual void Empty() { }
		int a;
		double b;
		TESTS(DoubleInPolymorphicStruct, a, b)
	};

	// The addition of a 64-bit type anywhere in this struct forces the vtable ptr to be occupy 8 bytes like previous
	struct Int64InPolymorphicStruct
	{
		virtual void Empty() { }
		int a;
		clcpp::int64 b;
		TESTS(Int64InPolymorphicStruct, a, b)
	};

	// Aggregation of a 64-bit struct generates the same behaviour
	struct DoubleStruct
	{
		double a;
	};
	struct DoubleStructInPolymorphicStruct
	{
		virtual void Empty() { }
		int a;
		DoubleStruct b;

		TESTS(DoubleStructInPolymorphicStruct, a, b);
	};


	// This will fail to reflect because of virtual inheritance but it highlights the behaviour of MSVC
	struct VirtualInheritanceBase
	{
	};
	struct VirtualInheritance : virtual public VirtualInheritanceBase
	{
		int a;
		TESTS(VirtualInheritance, a);
	};

	// This will also fail to reflect but is used to highlight virtual inheritance with polymorphic types
	struct VirtualInheritancePolymorphicBase
	{
		virtual void Empty() { }
	};
	struct VirtualInheritancePolymorphic : virtual public VirtualInheritancePolymorphicBase
	{
		int a;
		TESTS(VirtualInheritancePolymorphic, a);
	};

	// Same drill with 64-bit types - MSVC adds extra padding
	struct VirtualInheritance64bit : virtual public VirtualInheritanceBase
	{
		double a;
		TESTS(VirtualInheritance64bit, a);
	};

	// MSVC adds further padding it seems
	struct VirtualInheritancePolymorphic64bit : virtual public VirtualInheritancePolymorphicBase
	{
		double a;
		TESTS(VirtualInheritancePolymorphic64bit, a);
	};

	struct A
	{
		char a;
		char b;
		char c;
		char d;
		char e;

		TESTS(A, a, b, c, d, e);
	};

	struct B
	{
		char a;
		short b;
		char c;
		short d;
		char e;

		TESTS(B, a, b, c, d, e)
	};

	struct C
	{
		char a;
		short b;
		int c;
		long d;
		float e;
		double f;

		TESTS(C, a, b, c, d, e, f)
	};

	struct D
	{
		double a;
		float b;
		char c;
		char d;
		int e;
		long f;
		char g;
		double h;
		char i;
		short j;
		float k;
		float l;
		int m;
		short n;
		double o;
		short p;

		TESTS(D, a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p)
	};

	struct E : public D
	{
		double q;
		float r;
		char s;
		char t;
		int u;
		long v;
		char w;
		double x;
		char y;
		short z;

		TESTS(E, q, r, s, t, u, v, w, x, y, z)
	};

	struct F
	{
		A a;
		int b;
		B c;
		double d;
		C e;
		char f;
		D g;
		short h;
		E i;
		float j;

		TESTS(F, a, b, c, d, e, f, g, h, i, j)
	};

	// A constructor changes H to a POD-type and causes layout behaviour changes in I with clang
	struct H
	{
		H() { }
		double a;
		float b;

		TESTS(H, a, b)
	};
	struct I : public H
	{
		int c;

		TESTS(I, c)
	};


	struct J
	{
		virtual ~J() { }

		BEGIN_TEST(J) { }
	};
	struct K : public J
	{
		virtual ~K() { }
		int a;
		double b;

		TESTS(K, a, b)
	};
}



void TestOffsets(clcpp::Database& db)
{
	Offsets::A::Test(db);
	Offsets::B::Test(db);
	Offsets::C::Test(db);
	Offsets::D::Test(db);
	Offsets::E::Test(db);
	Offsets::F::Test(db);
	Offsets::PolymorphicStruct::Test(db);
	Offsets::DerivedPolymorphicStruct::Test(db);
	Offsets::DerivedPolymorphicWithPODBase::Test(db);
	Offsets::DoubleInPolymorphicStruct::Test(db);
	Offsets::Int64InPolymorphicStruct::Test(db);
	Offsets::DoubleStructInPolymorphicStruct::Test(db);
	Offsets::VirtualInheritance::Test(db);
	Offsets::VirtualInheritancePolymorphic::Test(db);
	Offsets::VirtualInheritance64bit::Test(db);
	Offsets::VirtualInheritancePolymorphic64bit::Test(db);
	Offsets::H::Test(db);
	Offsets::I::Test(db);
	Offsets::J::Test(db);
	Offsets::K::Test(db);
}
