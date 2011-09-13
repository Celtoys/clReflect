
#include <clcpp/clcpp.h>
#include <clutl/Serialise.h>
#include <clutl/Containers.h>

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

	// TODO: test nesting and enums.


	void Test(const char* name, const char* test)
	{
		printf("---------------------\n");
		printf("NAME: %s\n", name);
		printf("INP: %s\n", test);

		clutl::DataBuffer buffer(strlen(test));
		buffer.Write(test, strlen(test));
		buffer.ResetPosition();

		printf("OUT: ");
		clutl::JSONError error = clutl::LoadJSON(buffer, 0, 0);
		printf("\n");

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

	struct AllFields
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

		double f22;
		double f23;
		double f24;
		double f25;

		unsigned char f26;
		unsigned short f27;
		unsigned int f28;
		unsigned long f29;
		unsigned __int64 f30;

		Value e0;
		Value e1;
		Value e2;
	};
}


void TestSerialiseJSON(clcpp::Database& db)
{
	// Read the file into memory
	/*FILE* fp = fopen("jsontests/3.txt", "rb");
	fseek(fp, 0, SEEK_END);
	int len = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	clutl::DataBuffer buffer(len);
	fread((void*)buffer.ReadAt(0), 1, len, fp);
	fclose(fp);

	clutl::LoadJSON(buffer, 0, 0);*/

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

	clutl::DataBuffer buffer(2048);
	jsontest::AllFields a;
	clutl::SaveJSON(buffer, &a, clcpp_get_type(db, jsontest::AllFields));
	buffer.ResetPosition();
	jsontest::AllFields b(jsontest::NO_INIT);
	clutl::LoadJSON(buffer, &b, clcpp_get_type(db, jsontest::AllFields));
}