
#include <crcpp\Database.h>

#include <cstdio>
#include <cstring>


const crcpp::Primitive* crcpp::FindPrimitive(const CArray<const Primitive*>& primitives, Name name)
{
	int first = 0;
	int last = primitives.size() - 1;

	// Binary search
	while (first < last)
	{
		// Identify the mid point
		int mid = (first + last) / 2;
		unsigned int compare_hash = primitives[mid]->name.hash;

		if (name.hash > compare_hash)
		{
			// Shift search to local upper half
			first = mid + 1;
		}
		else if (name.hash < compare_hash)
		{
			// Shift search to local lower half
			last = mid - 1;
		}
		else
		{
			// Exact match found
			return primitives[mid];
		}
	}

	return 0;
}




crcpp::Database::Database()
	: m_NameTextData(0)
{
}


crcpp::Database::~Database()
{
	delete [] m_NameTextData;
}


bool crcpp::Database::Load(const char* filename)
{
	return false;
}


// TODO: Verify that the memory mapping constructions are not out-of-bounds