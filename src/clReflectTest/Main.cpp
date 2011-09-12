
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
#include <malloc.h>


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

	bool Read(void* dest, int size)
	{
		return fread(dest, 1, size, m_FP) == size;
	}

private:
	FILE* m_FP;
};


class Malloc : public clcpp::IAllocator
{
	void* Alloc(unsigned int size)
	{
		return malloc(size);
	}
	void Free(void* ptr)
	{
		free(ptr);
	}
};


extern void TestGetType(clcpp::Database& db);
extern void TestConstructorDestructor(clcpp::Database& db);
extern void TestAttributesFunc(clcpp::Database& db);
extern void TestSerialise(clcpp::Database& db);
extern void TestSerialiseJSON(clcpp::Database& db);
extern void TestOffsets(clcpp::Database& db);


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
	db.Load(&file, &allocator);
	TestGetType(db);
	TestConstructorDestructor(db);
	TestAttributesFunc(db);
	TestSerialise(db);
	TestOffsets(db);
	TestSerialiseJSON(db);

	return 0;
}