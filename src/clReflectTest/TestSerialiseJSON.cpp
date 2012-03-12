
#include <clcpp/clcpp.h>
#include <clutl/Serialise.h>

#include <stdio.h>
#include <string.h>


namespace
{
	// strings with hex digits and escape sequences
	// hex digits with invalid characters
	// invalid escape sequences
	// open object without close
	// open array without close
	// pair without comma
	// string without closing quote
	// pair without value
	// pair without string


	void Test(const char* name, const char* test)
	{
		printf("---------------------\n");
		printf("NAME: %s\n", name);
		printf("INP: %s\n", test);

		clutl::WriteBuffer write_buffer;
		write_buffer.Write(test, strlen(test));
		clutl::ReadBuffer read_buffer(write_buffer);

		clutl::JSONError error = clutl::LoadJSON(read_buffer, 0, (clcpp::Type*)0);
		if (error.code == clutl::JSONError::NONE)
		{
			printf("PASS\n");
		}
		else
		{
			printf("FAIL (%d, %d): ", error.line, error.column);

			switch (error.code)
			{
			case (clutl::JSONError::UNEXPECTED_END_OF_DATA): printf("UNEXPECTED_END_OF_DATA\n"); break;
			case (clutl::JSONError::EXPECTING_HEX_DIGIT): printf("EXPECTING_HEX_DIGIT\n"); break;
			case (clutl::JSONError::EXPECTING_DIGIT): printf("EXPECTING_DIGIT\n"); break;
			case (clutl::JSONError::UNEXPECTED_CHARACTER): printf("UNEXPECTED_CHARACTER\n"); break;
			case (clutl::JSONError::INVALID_KEYWORD): printf("INVALID_KEYWORD\n"); break;
			case (clutl::JSONError::INVALID_ESCAPE_SEQUENCE): printf("INVALID_ESCAPE_SEQUENCE\n"); break;
			case (clutl::JSONError::UNEXPECTED_TOKEN): printf("UNEXPECTED_TOKEN\n"); break;
			default: break;
			}
		}
	}
}


clcpp_reflect(jsontest)
namespace jsontest
{
	enum NoInit { NO_INIT };

	enum Value
	{
		VALUE_A,
		WHATEVER,
		YUP
	};


	struct BaseStruct
	{
		BaseStruct()
			: a(100)
			, b(101)
			, c(102)
			, d(103)
		{
		}

		BaseStruct(NoInit)
		{
		}

		bool operator == (const BaseStruct& rhs) const
		{
			if (a != rhs.a)
				return false;
			if (b != rhs.b)
				return false;
			if (c != rhs.c)
				return false;
			if (d != rhs.d)
				return false;

			return true;
		}

		int a;
		double b;
		char c;
		short d;
	};

	struct NestedStruct
	{
		NestedStruct()
			: x(1)
			, y(2)
			, z(3)
		{
		}

		NestedStruct(NoInit)
		{
		}

		bool operator == (const NestedStruct& rhs) const
		{
			if (x != rhs.x)
				return false;
			if (y != rhs.y)
				return false;
			if (z != rhs.z)
				return false;

			return true;
		}

		float x;
		double y;
		char z;
	};

	struct AllFields : public BaseStruct
	{
		AllFields()
			: f0(1)

			, f1(1)
			, f2(113)
			, f3(-98)

			, f4(3)
			, f5(27645)
			, f6(-1234)

			, f7(6)
			, f8(1483720389)
			, f9(-937201923)

			, f10(9)
			, f11(1483720389)
			, f12(-937201923)

			, f13(4)
			, f14(9223372036854775807)
			, f15(-223372036854775807)

			, f16(0)
			, f17(1)
			, f18(0.125)
			, f19(3.4028234663852886e+38f)
			, f20(-1)
			, f21(0.00390625)

			, f22(0)
			, f23(1)
			, f24(0.125)
			, f25(1.7976931348623157e+308)

			, f26(0xFF)
			, f27(0xFFFF)
			, f28(0xFFFFFFFF)
			, f29(0xFFFFFFFF)
			, f30(0xFFFFFFFFFFFFFFFF)

			, e0(VALUE_A)
			, e1(WHATEVER)
			, e2(YUP)
		{
		}
		AllFields(NoInit)
			: BaseStruct(NO_INIT)
			, nested0(NO_INIT)
			, nested1(NO_INIT)
			, nested2(NO_INIT)
		{
		}

		bool f0;

		char f1;
		char f2;
		char f3;

		short f4;
		short f5;
		short f6;

		int f7;
		int f8;
		int f9;

		long f10;
		long f11;
		long f12;

		__int64 f13;
		__int64 f14;
		__int64 f15;

		float f16;
		float f17;
		float f18;
		float f19;
		float f20;
		float f21;

		NestedStruct nested0;

		double f22;
		double f23;
		double f24;
		double f25;

		NestedStruct nested1;

		unsigned char f26;
		unsigned short f27;
		unsigned int f28;
		unsigned long f29;
		unsigned __int64 f30;

		Value e0;
		Value e1;
		Value e2;

		NestedStruct nested2;

		bool failed(const char* field) const
		{
			printf("Comparison of '%s' failed\n", field);
			return false;
		}

		bool operator == (const AllFields& rhs) const
		{
			if (!(*(BaseStruct*)this == (BaseStruct&)rhs))
				return false;

			if (f0 != rhs.f0)
				return failed("f0");
			if (f1 != rhs.f1)
				return failed("f1");
			if (f2 != rhs.f2)
				return failed("f2");
			if (f3 != rhs.f3)
				return failed("f3");
			if (f4 != rhs.f4)
				return failed("f4");
			if (f5 != rhs.f5)
				return failed("f5");
			if (f6 != rhs.f6)
				return failed("f6");
			if (f7 != rhs.f7)
				return failed("f7");
			if (f8 != rhs.f8)
				return failed("f8");
			if (f9 != rhs.f9)
				return failed("f9");
			if (f10 != rhs.f10)
				return failed("f10");
			if (f11 != rhs.f11)
				return failed("f11");
			if (f12 != rhs.f12)
				return failed("f12");
			if (f13 != rhs.f13)
				return failed("f13");
			if (f14 != rhs.f14)
				return failed("f14");
			if (f15 != rhs.f15)
				return failed("f15");
			if (f16 != rhs.f16)
				return failed("f16");
			if (f17 != rhs.f17)
				return failed("f17");
			if (f18 != rhs.f18)
				return failed("f18");
			if (f19 != rhs.f19)
				return failed("f19");
			if (f20 != rhs.f20)
				return failed("f20");
			if (f21 != rhs.f21)
				return failed("f21");
			if (f22 != rhs.f22)
				return failed("f22");
			if (f23 != rhs.f23)
				return failed("f23");
			if (f24 != rhs.f24)
				return failed("f24");
			if (f25 != rhs.f25)
				return failed("f25");
			if (f26 != rhs.f26)
				return failed("f26");
			if (f27 != rhs.f27)
				return failed("f27");
			if (f28 != rhs.f28)
				return failed("f28");
			if (f29 != rhs.f29)
				return failed("f29");
			if (f30 != rhs.f30)
				return failed("f30");
			if (e0 != rhs.e0)
				return failed("f31");
			if (e1 != rhs.e1)
				return failed("f32");
			if (e2 != rhs.e2)
				return failed("f33");

			if (!(nested0 == rhs.nested0))
				return failed("nested0");
			if (!(nested1 == rhs.nested1))
				return failed("nested1");
			if (!(nested2 == rhs.nested2))
				return failed("nested2");

			return true;
		}
	};
}


void TestSerialiseJSON(clcpp::Database& db)
{
	Test("EmptyObject", "{ }");
	Test("NestedEmptyObjects", "{ \"nested\" : { } }");
	Test("EmptyArrayObject", "{ \"nested\" : [ ] }");

	Test("String", "{ \"string\" : \"val\" }");
	Test("Integer", "{ \"integer\" : 123 }");
	Test("NegInteger", "{ \"integer\" : -123 }");
	Test("Decimal", "{ \"decimal\" : 123.123 }");
	Test("NegDecimal", "{ \"decimal\" : -123.123 }");

	Test("DecimalE0", "{ \"decimal\" : 123e4 }");
	Test("DecimalE1", "{ \"decimal\" : 123e+4 }");
	Test("DecimalE2", "{ \"decimal\" : 123e-4 }");
	Test("DecimalE3", "{ \"decimal\" : 123E4 }");
	Test("DecimalE4", "{ \"decimal\" : 123E+4 }");
	Test("DecimalE5", "{ \"decimal\" : 123E-4 }");
	Test("DecimalE6", "{ \"decimal\" : 123.123e4 }");
	Test("DecimalE7", "{ \"decimal\" : 123.123e+4 }");
	Test("DecimalE8", "{ \"decimal\" : 123.123e-4 }");
	Test("DecimalE9", "{ \"decimal\" : 123.123E4 }");
	Test("DecimalE10", "{ \"decimal\" : 123.123E+4 }");
	Test("DecimalE11", "{ \"decimal\" : 123.123E-4 }");

	Test("NegDecimalE0", "{ \"decimal\" : -123e4 }");
	Test("NegDecimalE1", "{ \"decimal\" : -123e+4 }");
	Test("NegDecimalE2", "{ \"decimal\" : -123e-4 }");
	Test("NegDecimalE3", "{ \"decimal\" : -123E4 }");
	Test("NegDecimalE4", "{ \"decimal\" : -123E+4 }");
	Test("NegDecimalE5", "{ \"decimal\" : -123E-4 }");
	Test("NegDecimalE6", "{ \"decimal\" : -123.123e4 }");
	Test("NegDecimalE7", "{ \"decimal\" : -123.123e+4 }");
	Test("NegDecimalE8", "{ \"decimal\" : -123.123e-4 }");
	Test("NegDecimalE9", "{ \"decimal\" : -123.123E4 }");
	Test("NegDecimalE10", "{ \"decimal\" : -123.123E+4 }");
	Test("NegDecimalE11", "{ \"decimal\" : -123.123E-4 }");

	Test("EscapeSequences", "{ \"string\" : \" \\\" \\\\ \\/ \\b \\f \\n \\r \\t \\u0123 \" }");

	Test("True", "{ \"value\" : true }");
	Test("False", "{ \"value\" : false }");
	Test("Null", "{ \"value\" : null }");

	Test("StringErrorStart", "{ \"string\" : \"");
	Test("StringErrorMid", "{ \"string\" : \"asd");
	Test("StringErrorEscape", "{ \"string\" : \\");
	Test("StringErrorHexOverflow", "{ \"string\" : \"\\u1");
	Test("StringErrorHexInvalid", "{ \"string\" : \"\\ug000\"");
	Test("StringErrorInvalidEscape", "{ \"string\" : \"\\y\"");

	Test("IntegerErrorSignOverflow", "{ \"integer\" : -");
	Test("IntegerErrorNegOverflow", "{ \"integer\" : -123");
	Test("IntegerErrorIntegerOverflow", "{ \"integer\" : 123");

	Test("DecimalErrorOverflow", "{ \"decimal\" : 123.");
	Test("DecimalErrorDigitOverflow", "{ \"decimal\" : 123.123");
	Test("DecimalErrorEOverflow", "{ \"decimal\" : 123e");
	Test("DecimalErrorEOverflowP", "{ \"decimal\" : 123e+");
	Test("DecimalErrorEOverflowN", "{ \"decimal\" : 123e-");
	Test("DecimalErrorEOverflowE", "{ \"decimal\" : 123e123");

	Test("PairErrorNoString", "{ : \"value\" }");
	Test("PairErrorNoValue", "{ \"string\" : }");
	Test("PairErrorInvalidValue", "{ \"string\" : x }");

	Test("ErrorTrueOverflow", "{ \"value\" : tru");
	Test("ErrorFalseOverflow", "{ \"value\" : fal");
	Test("ErrorNullOverflow", "{ \"value\" : nu");
	Test("ErrorTrueInvalidKeyword", "{ \"value\" : tru ");
	Test("ErrorFalseInvalidKeyword", "{ \"value\" : fal ");
	Test("ErrorNullInvalidKeyword", "{ \"value\" : nu ");

	clutl::WriteBuffer write_buffer;
	jsontest::AllFields a;
	clutl::SaveJSON(write_buffer, &a, clcpp::GetType<jsontest::AllFields>(), clutl::JSONFlags::EMIT_HEX_FLOATS);
	clutl::ReadBuffer read_buffer(write_buffer);
	jsontest::AllFields b(jsontest::NO_INIT);
	clutl::LoadJSON(read_buffer, &b, clcpp::GetType<jsontest::AllFields>());

	if (a == b)
		printf("STRUCT PASS!\n");
	else
		printf("STRUCT FAIL!\n");
}