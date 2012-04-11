
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011 Don Williamson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ===============================================================================
//

#include <clcpp/clcpp.h>

#include <cstdio>
#if defined(CLCPP_USING_MSVC)
#include <malloc.h>
#else
#include <stdlib.h>
#endif
#include <stdarg.h>
#include <errno.h>


#ifdef _MSC_VER
extern "C" int snprintf(char* dest, unsigned int n, const char* fmt, ...)
{
	// Unfortunately snprintf is not a standardised function and MSVC has its own "safe" version
	va_list args;
	va_start(args, fmt);
	int count = vsnprintf_s(dest, n, _TRUNCATE, fmt, args);
	va_end(args);

	// This seems to be he only valid way of detecting truncation
	if (errno == 0 && count == -1)
		return n - 1;

	return count;
}
#endif


class StdFile : public clcpp::IFile
{
public:
	StdFile(const char* filename)
	{
		m_FP = fopen(filename, "rb");
		if (m_FP == 0)
		{
			return;
		}
	}

	~StdFile()
	{
		if (m_FP != 0)
		{
			fclose(m_FP);
		}
	}

	bool IsOpen() const
	{
		return m_FP != 0;
	}

	bool Read(void* dest, clcpp::size_type size)
	{
		return fread(dest, 1, size, m_FP) == size;
	}

private:
	FILE* m_FP;
};


class Malloc : public clcpp::IAllocator
{
	void* Alloc(clcpp::size_type size)
	{
		return malloc(size);
	}
	void Free(void* ptr)
	{
		free(ptr);
	}
};


extern void TestGetType(clcpp::Database& db);
extern void TestArraysFunc(clcpp::Database& db);
extern void TestConstructorDestructor(clcpp::Database& db);
extern void TestAttributesFunc(clcpp::Database& db);
extern void TestSerialise(clcpp::Database& db);
extern void TestSerialiseJSON(clcpp::Database& db);
extern void TestOffsets(clcpp::Database& db);
extern void TestTypedefsFunc(clcpp::Database& db);
extern void TestFunctionSerialise(clcpp::Database& db);


int main()
{
#ifdef _DEBUG
	StdFile file("../../build/bin/Debug/clReflectTest.cppbin");
#else
	StdFile file("../../build/bin/Release/clReflectTest.cppbin");
#endif
	if (!file.IsOpen())
	{
		return 1;
	}

	Malloc allocator;
	clcpp::Database db;
	db.Load(&file, &allocator, 0, 0);
	TestGetType(db);
	TestConstructorDestructor(db);
	TestArraysFunc(db);
	TestAttributesFunc(db);
	TestSerialise(db);
	TestOffsets(db);
	TestSerialiseJSON(db);
	TestTypedefsFunc(db);
	TestFunctionSerialise(db);

	return 0;
}
