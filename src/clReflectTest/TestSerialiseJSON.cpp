
#include <clcpp/clcpp.h>
#include <clutl/Serialise.h>
#include <clutl/Containers.h>

#include <stdio.h>
#include <string.h>


namespace
{
	const char* g_TestEmptyObject = "{ }";
	const char* g_TestNestedEmptyObjects = "{ \"nested\" : { } }";
	const char* g_TestEmptyArrayObject = "{ \"nested\" : [ ] }";

	const char* g_TestString = "{ \"string\" : \"val\" }";
	const char* g_TestInteger = "{ \"integer\" : 123 }";
	const char* g_TestNegInteger = "{ \"integer\" : -123 }";
	const char* g_TestDecimal = "{ \"decimal\" : 123.123 }";
	const char* g_TestNegDecimal = "{ \"decimal\" : -123.123 }";

	const char* g_TestDecimalE0 = "{ \"decimal\" : 123e4 }";
	const char* g_TestDecimalE1 = "{ \"decimal\" : 123e+4 }";
	const char* g_TestDecimalE2 = "{ \"decimal\" : 123e-4 }";
	const char* g_TestDecimalE3 = "{ \"decimal\" : 123E4 }";
	const char* g_TestDecimalE4 = "{ \"decimal\" : 123E+4 }";
	const char* g_TestDecimalE5 = "{ \"decimal\" : 123E-4 }";
	const char* g_TestDecimalE6 = "{ \"decimal\" : 123.123e4 }";
	const char* g_TestDecimalE7 = "{ \"decimal\" : 123.123e+4 }";
	const char* g_TestDecimalE8 = "{ \"decimal\" : 123.123e-4 }";
	const char* g_TestDecimalE9 = "{ \"decimal\" : 123.123E4 }";
	const char* g_TestDecimalE10 = "{ \"decimal\" : 123.123E+4 }";
	const char* g_TestDecimalE11 = "{ \"decimal\" : 123.123E-4 }";

	const char* g_TestNegDecimalE0 = "{ \"decimal\" : -123e4 }";
	const char* g_TestNegDecimalE1 = "{ \"decimal\" : -123e+4 }";
	const char* g_TestNegDecimalE2 = "{ \"decimal\" : -123e-4 }";
	const char* g_TestNegDecimalE3 = "{ \"decimal\" : -123E4 }";
	const char* g_TestNegDecimalE4 = "{ \"decimal\" : -123E+4 }";
	const char* g_TestNegDecimalE5 = "{ \"decimal\" : -123E-4 }";
	const char* g_TestNegDecimalE6 = "{ \"decimal\" : -123.123e4 }";
	const char* g_TestNegDecimalE7 = "{ \"decimal\" : -123.123e+4 }";
	const char* g_TestNegDecimalE8 = "{ \"decimal\" : -123.123e-4 }";
	const char* g_TestNegDecimalE9 = "{ \"decimal\" : -123.123E4 }";
	const char* g_TestNegDecimalE10 = "{ \"decimal\" : -123.123E+4 }";
	const char* g_TestNegDecimalE11 = "{ \"decimal\" : -123.123E-4 }";

	const char* g_TestEscapeSequences = "{ \"string\" : \" \\\" \\\\ \\/ \\b \\f \\n \\r \\t \\u0123 \" }";

	const char* g_TestTrue = "{ \"value\" : true }";
	const char* g_TestFalse = "{ \"value\" : false }";
	const char* g_TestNull = "{ \"value\" : null }";

	const char* g_TestStringErrorStart = "{ \"string\" : \"";
	const char* g_TestStringErrorMid = "{ \"string\" : \"asd";
	const char* g_TestStringErrorEscape = "{ \"string\" : \\";
	const char* g_TestStringErrorHexOverflow = "{ \"string\" : \\u1";
	const char* g_TestStringErrorHexInvalid = "{ \"string\" : \\ug000";
	
	const char* g_TestIntegerErrorSignOverflow = "{ \"integer\" : -";
	const char* g_TestIntegerErrorNegOverflow = "{ \"integer\" : -123";
	const char* g_TestIntegerErrorIntegerOverflow = "{ \"integer\" : 123";

	const char* g_TestDecimalErrorOverflow = "{ \"decimal\" : 123.";
	const char* g_TestDecimalErrorDigitOverflow = "{ \"decimal\" : 123.123";
	const char* g_TestDecimalErrorEOverflow = "{ \"decimal\" : 123e";
	const char* g_TestDecimalErrorEOverflowP = "{ \"decimal\" : 123e+";
	const char* g_TestDecimalErrorEOverflowN = "{ \"decimal\" : 123e-";
	const char* g_TestDecimalErrorEOverflowE = "{ \"decimal\" : 123e123";

	const char* g_TestPairErrorNoString = "{ : \"value\" }";
	const char* g_TestPairErrorNoValue = "{ \"string\" : }";
	const char* g_TestPairErrorInvalidValue = "{ \"string\" : x }";

	const char* g_TestErrorTrueOverflow = "{ \"value\" : tru";
	const char* g_TestErrorFalseOverflow = "{ \"value\" : fal";
	const char* g_TestErrorNullOverflow = "{ \"value\" : nu";

	// strings with hex digits and escape sequences
	// hex digits with invalid characters
	// invalid escape sequences
	// open object without close
	// open array without close
	// pair without comma
	// string without closing quote
	// pair without value
	// pair without string


	// garbage data, overflows
	// overflow reading hex digits

	void Test(const char* test)
	{
		clutl::DataBuffer buffer(strlen(test));
		buffer.Write(test, strlen(test));
		buffer.Reset();
		clutl::LoadJSON(buffer, 0, 0);
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

	Test(g_TestEmptyObject);
	Test(g_TestNestedEmptyObjects);
	Test(g_TestEmptyArrayObject);

	Test(g_TestString);
	Test(g_TestInteger);
	Test(g_TestDecimal);

	Test(g_TestEscapeSequences);

	Test(g_TestEmptyObject);
	Test(g_TestNestedEmptyObjects);
	Test(g_TestEmptyArrayObject);

	Test(g_TestString);
	Test(g_TestInteger);
	Test(g_TestNegInteger);
	Test(g_TestDecimal);
	Test(g_TestNegDecimal);

	Test(g_TestDecimalE0);
	Test(g_TestDecimalE1);
	Test(g_TestDecimalE2);
	Test(g_TestDecimalE3);
	Test(g_TestDecimalE4);
	Test(g_TestDecimalE5);
	Test(g_TestDecimalE6);
	Test(g_TestDecimalE7);
	Test(g_TestDecimalE8);
	Test(g_TestDecimalE9);
	Test(g_TestDecimalE10);
	Test(g_TestDecimalE11);

	Test(g_TestNegDecimalE0);
	Test(g_TestNegDecimalE1);
	Test(g_TestNegDecimalE2);
	Test(g_TestNegDecimalE3);
	Test(g_TestNegDecimalE4);
	Test(g_TestNegDecimalE5);
	Test(g_TestNegDecimalE6);
	Test(g_TestNegDecimalE7);
	Test(g_TestNegDecimalE8);
	Test(g_TestNegDecimalE9);
	Test(g_TestNegDecimalE10);
	Test(g_TestNegDecimalE11);

	Test(g_TestEscapeSequences);

	Test(g_TestTrue);
	Test(g_TestFalse);
	Test(g_TestNull);
}