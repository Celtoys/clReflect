
#include <crcpp/crcpp.h>

#include <cstdio>


class StdFile : public crcpp::IFile
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


int main()
{
	StdFile file("../../bin/Debug/ClangReflectTest.cppbin");
	if (!file.IsOpen())
	{
		return 1;
	}

	crcpp::Database db;
	db.Load(&file);

	return 0;
}