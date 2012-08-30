
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
#if defined(CLCPP_USING_MSVC)
#include <malloc.h>
#else
#include <stdlib.h>
#endif
#include <stdarg.h>
#include <errno.h>


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
	// Ensure the cppbin file is in the same directory as the executable
	StdFile file("clReflectTest.cppbin");
	if (!file.IsOpen())
		return 1;

	Malloc allocator;
	clcpp::Database db;
	if (!db.Load(&file, &allocator, 0))
		return 1;

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
