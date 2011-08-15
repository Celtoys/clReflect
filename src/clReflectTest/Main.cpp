
#include <clcpp/clcpp.h>

#include <cstdio>


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


extern void TestGetType(clcpp::Database& db);
extern void TestConstructorDestructor(clcpp::Database& db);
extern void TestAttributesFunc(clcpp::Database& db);


int main()
{
	StdFile file("../../bin/Debug/clReflectTest.cppbin");
	if (!file.IsOpen())
	{
		return 1;
	}

	clcpp::Database db;
	db.Load(&file);
	TestGetType(db);
	TestConstructorDestructor(db);
	TestAttributesFunc(db);

	return 0;
}