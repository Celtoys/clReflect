
#pragma once


#include <cassert>


//
// Compile-time means of identifying built-in types (only those specified below)
//
template <typename TYPE> inline bool is_builtin() { return false; }
template <> inline bool is_builtin<bool>() { return true; }
template <> inline bool is_builtin<char>() { return true; }
template <> inline bool is_builtin<unsigned char>() { return true; }
template <> inline bool is_builtin<short>() { return true; }
template <> inline bool is_builtin<unsigned short>() { return true; }
template <> inline bool is_builtin<int>() { return true; }
template <> inline bool is_builtin<unsigned int>() { return true; }
template <> inline bool is_builtin<long>() { return true; }
template <> inline bool is_builtin<unsigned long>() { return true; }
template <> inline bool is_builtin<float>() { return true; }
template <> inline bool is_builtin<double>() { return true; }


//
// The requirements of this class are that you can grow the data buffer without
// invalidating previously allocated pointers. Obviously, wrapping something like
// std::vector won't work in this case. For now I'm just pre-allocating the worst
// case amount of memory and returning pointers from within that. However, if these
// databases get bigger it might be worth using VirtualAlloc to reserve a large
// contiguous set of pages which are committed on demand. Before that, though: the
// database really shouldn't be any bigger than a couple of megabytes!
//
class StackAllocator
{
public:
	StackAllocator(int size)
		: m_Data(new char[size])
		, m_Size(size)
		, m_Offset(0)
	{
	}

	template <typename TYPE>
	TYPE* Alloc(unsigned int count)
	{
		// Allocate the required amount of bytes
		TYPE* data = (TYPE*)(m_Data + m_Offset);
		m_Offset += count * sizeof(TYPE);
		assert(m_Offset <= m_Size && "Stack allocator overflowed");

		// Default construct non-builtin types
		if (!is_builtin<TYPE>())
		{
			for (unsigned int i = 0; i < count; i++)
			{
				new (data + i) TYPE();
			}
		}

		return data;
	}

private:
	char* m_Data;
	unsigned int m_Size;
	unsigned int m_Offset;
};


