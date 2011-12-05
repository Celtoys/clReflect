
//
// ===============================================================================
// clReflect, StackAllocator.h - Memory allocation from a stack.
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

#pragma once


#include <cassert>
#include <clcpp/Core.h>


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
template <> inline bool is_builtin<__int64>() { return true; }
template <> inline bool is_builtin<unsigned __int64>() { return true; }


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
				new (data + i) TYPE;
			}
		}

		return data;
	}

	template <typename TYPE>
	void Alloc(clcpp::CArray<TYPE>& array, int size)
	{
		// Direct member copy of an array constructed with pre-allocated data
		TYPE* data = Alloc<TYPE>(size);
		array.shallow_copy(clcpp::CArray<TYPE>(data, size));
	}

	const void* GetData() const { return m_Data; }
	unsigned int GetSize() const { return m_Size; }
	unsigned int GetAllocatedSize() const { return m_Offset; }

private:
	char* m_Data;
	unsigned int m_Size;
	unsigned int m_Offset;
};


