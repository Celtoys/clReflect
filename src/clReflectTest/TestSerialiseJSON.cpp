
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


	void Test(const char* name, const char* test)
	{
		printf("---------------------\n");
		printf("NAME: %s\n", name);
		printf("INP: %s\n", test);

		clutl::DataBuffer buffer(strlen(test));
		buffer.Write(test, strlen(test));
		buffer.Reset();

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
			}
		}
	}
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
}