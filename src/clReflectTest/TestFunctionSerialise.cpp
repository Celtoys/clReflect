
#include <clcpp/clcpp.h>
#include <clutl/SerialiseFunction.h>
#include <stdio.h>


clcpp_reflect(Funcs)
namespace Funcs
{
	// Some structures for passing between functions
	#pragma pack(push, 1)
	struct EmptyStruct { };
	struct CharStruct { CharStruct() : x(1) { } char x; };
	struct ThreeStruct { ThreeStruct() : x(1), y(2), z(3) { } char x, y, z; };
	struct DoubleStruct { DoubleStruct() : x(1) { } double x; };
	struct OddStruct { OddStruct() { data[10] = 11; } char data[11]; };
	struct BigStruct { BigStruct() { data[127] = 126; } char data[128]; };
	#pragma pack(pop)


	const clcpp::Function* GetFunc(clcpp::Database& db, const char* name)
	{
		return db.GetFunction(db.GetName(name).hash);
	}


	void Write(clutl::WriteBuffer& buf, const char* data)
	{
		buf.Reset();
		buf.WriteStr(data);
	}


	void Call(clutl::ParameterObjectCache& poc, const clcpp::Function* function, const char* data)
	{
		printf("TEST: %s\n", data);
		clutl::WriteBuffer wb;
		wb.WriteStr(data);

		clutl::ReadBuffer rb(wb);
		if (!clutl::BuildParameterObjectCache_JSON(poc, function, rb))
			printf("   FAILED\n");
		else
		{
			clutl::CallFunction_x86_32_msvc_cdecl(function, poc.GetParameters());
		}
	}


	void A()
	{
		printf("A\n");
	}
	void B(char a, char b)
	{
		printf("B char: %d\n", a);
	}
	void C(short a)
	{
		printf("C short: %d\n", a);
	}
	void D(const int& a)
	{
		printf("D int: %d\n", a);
	}
	void E(float a)
	{
		printf("E float: %f\n", a);
	}
	void F(double a)
	{
		printf("F double: %f\n", a);
	}
	void G(EmptyStruct a)
	{
		printf("G EmptyStruct\n");
	}
	void H(CharStruct a)
	{
		printf("H CharStruct: %d\n", a.x);
	}
	void I(ThreeStruct a)
	{
		printf("I ThreeStruct: %d, %d, %d\n", a.x, a.y, a.z);
	}
	void J(DoubleStruct a)
	{
		printf("J DoubleStruct: %f\n", a.x);
	}
	void K(OddStruct a)
	{
		printf("K OddStruct: %d\n", a.data[10]);
	}
	void L(const BigStruct& a)
	{
		printf("L BigStruct: %d\n", a.data[127]);
	}

	void M(char b, short c, int d, float e, double f, EmptyStruct& g, CharStruct h, ThreeStruct& i, DoubleStruct j, OddStruct& k, BigStruct l)
	{
		printf("--------------------------\n");
		printf("M\n");
		A();
		B(b, b);
		C(c);
		D(d);
		E(e);
		F(f);
		G(g);
		H(h);
		I(i);
		J(j);
		K(k);
		L(l);
		printf("--------------------------\n");
	}


	struct Container
	{
		Container() : text("Container")
		{
		}

		void A()
		{
			printf("%s::A\n", text);
		}
		void B(char a, char b)
		{
			printf("%s::B char: %d\n", text, a);
		}
		void C(short a)
		{
			printf("%s::C short: %d\n", text, a);
		}
		void D(const int& a)
		{
			printf("%s::D int: %d\n", text, a);
		}
		void E(float a)
		{
			printf("%s::E float: %f\n", text, a);
		}
		void F(double a)
		{
			printf("%s::F double: %f\n", text, a);
		}
		void G(EmptyStruct a)
		{
			printf("%s::G EmptyStruct\n", text);
		}
		void H(CharStruct a)
		{
			printf("%s::H CharStruct: %d\n", text, a.x);
		}
		void I(ThreeStruct a)
		{
			printf("%s::I ThreeStruct: %d, %d, %d\n", text, a.x, a.y, a.z);
		}
		void J(DoubleStruct a)
		{
			printf("%s::J DoubleStruct: %f\n", text, a.x);
		}
		void K(OddStruct a)
		{
			printf("%s::K OddStruct: %d\n", text, a.data[10]);
		}
		void L(const BigStruct& a)
		{
			printf("%s::L BigStruct: %d\n", text, a.data[127]);
		}

		void M(char b, short c, int d, float e, double f, EmptyStruct& g, CharStruct h, ThreeStruct& i, DoubleStruct j, OddStruct& k, BigStruct l)
		{
			printf("--------------------------\n");
			printf("%s::M\n", text);
			A();
			B(b, b);
			C(c);
			D(d);
			E(e);
			F(f);
			G(g);
			H(h);
			I(i);
			J(j);
			K(k);
			L(l);
			printf("--------------------------\n");
		}

		const char* text;
	};
}


clcpp_impl_class(Funcs::OddStruct)
clcpp_impl_class(Funcs::BigStruct)


void TestFunctionSerialise(clcpp::Database& db)
{
	printf("\n");
	printf("===========================================================================================\n");
	printf("\n");

	using namespace Funcs;

	EmptyStruct es;
	CharStruct cs;
	ThreeStruct ts;
	DoubleStruct ds;
	OddStruct os;
	BigStruct bs;

	A();
	B(1, 1);
	C(2);
	D(3);
	E(4);
	F(5);
	G(es);
	H(cs);
	I(ts);
	J(ds);
	K(os);
	L(bs);
	M(1, 2, 3, 4, 5, es, cs, ts, ds, os, bs);

	const clcpp::Function* a = GetFunc(db, "Funcs::A");
	const clcpp::Function* b = GetFunc(db, "Funcs::B");
	const clcpp::Function* c = GetFunc(db, "Funcs::C");
	const clcpp::Function* d = GetFunc(db, "Funcs::D");
	const clcpp::Function* e = GetFunc(db, "Funcs::E");
	const clcpp::Function* f = GetFunc(db, "Funcs::F");
	const clcpp::Function* g = GetFunc(db, "Funcs::G");
	const clcpp::Function* h = GetFunc(db, "Funcs::H");
	const clcpp::Function* i = GetFunc(db, "Funcs::I");
	const clcpp::Function* j = GetFunc(db, "Funcs::J");
	const clcpp::Function* k = GetFunc(db, "Funcs::K");
	const clcpp::Function* l = GetFunc(db, "Funcs::L");
	const clcpp::Function* m = GetFunc(db, "Funcs::M");

	clutl::ParameterObjectCache poc;
	Call(poc, a, "[ ]");
	Call(poc, b, "[ 2, 3 ]");
	Call(poc, c, "[ 3 ]");
	Call(poc, d, "[ 4 ]");
	Call(poc, e, "[ 5 ]");
	Call(poc, f, "[ 6 ]");
	Call(poc, g, "[ { } ]");
	Call(poc, h, "[ { \"x\":2 } ]");
	Call(poc, i, "[ { \"x\":2, \"y\":3, \"z\":4 } ]");
	Call(poc, j, "[ { \"x\":2 } ]");
	Call(poc, k, "[ { } ]");
	Call(poc, l, "[ { } ]");
	Call(poc, m, "[ 2, 3, 4, 5, 6, { }, { \"x\":2 }, { \"x\":2, \"y\":3, \"z\":4 }, { \"x\": 2}, { }, { } ]");
}