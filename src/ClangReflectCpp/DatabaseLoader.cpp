
#include "DatabaseLoader.h"

#include <cstdio>
#include <cstring>


namespace
{
	template <typename TYPE> void Read(TYPE& dest, FILE* fp)
	{
		// Anything with no overload of Read is a straight POD read
		fread(&dest, sizeof(dest), 1, fp);
	}


	template <typename TYPE> TYPE Read(FILE* fp)
	{
		TYPE temp;
		Read(temp, fp);
		return temp;
	}
}


crcpp::DatabaseFileHeader::DatabaseFileHeader()
	: version(1)
{
	memcpy(signature, "crcppdb", sizeof(signature));
}
