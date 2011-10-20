
//
// ===============================================================================
// clReflect, Core.h - Core functionality required by the runtime C++ API.
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


//
// The C++ standard specifies that use of default placement new does not require inclusion of <new>.
// However, MSVC2005 disagrees and requires this. Since I don't want any CRT dependencies in the library,
// I don't want to include that. However, the C++ standard also states that implementing your own
// default new is illegal.
//
// I could just be pragmatic and ignore that (I have done for as long as I've known of the existence
// of placement new). Or I could do this... wrap pointers in a specific type that forwards to its own
// placement new, treating it like an allocator that returns the wrapped pointer.
//
namespace clcpp
{
	namespace internal
	{
		struct PtrWrapper
		{
		};
	}
}


//
// Placement new for the PtrWrapper logic specified above, which required matching delete
//
inline void* operator new (unsigned int size, const clcpp::internal::PtrWrapper& p)
{
	return (void*)&p;
}
inline void operator delete (void*, const clcpp::internal::PtrWrapper&)
{
}


namespace clcpp
{
	namespace internal
	{
		//
		// Trivial assert for PC only, currently
		//
		inline void Assert(bool expression)
		{
			if (expression == false)
			{
				__asm
				{
					int 3h
				}
			}
		}


		//
		// Hashes the specified data into a 32-bit value
		//
		unsigned HashData(const void* data, int length);


		//
		// Hashes the full string into a 32-bit value
		//
		unsigned int HashNameString(const char* name_string);


		//
		// Combines two hashes by using the first one as a seed and hashing the second one
		//
		unsigned int MixHashes(unsigned int a, unsigned int b);
	}


	//
	// Simple allocator interface for abstracting allocations made by the runtime.
	//
	struct IAllocator
	{
		virtual void* Alloc(unsigned int size) = 0;
		virtual void Free(void* ptr) = 0;
	};


	//
	// Wrapper around a classic C-style array.
	//
	template <typename TYPE>
	class CArray
	{
	public:
		// Initialise an empty array
		CArray()
			: m_Size(0)
			, m_Data(0)
			, m_Allocator(0)
		{
		}

		// Initialise with array count and allocator
		CArray(unsigned int size, IAllocator* allocator)
			: m_Size(size)
			, m_Data(0)
			, m_Allocator(allocator)
		{
			// Allocate and call the constructor for each element
			m_Data = (TYPE*)m_Allocator->Alloc(m_Size * sizeof(TYPE));
			for (unsigned int i = 0; i < m_Size; i++)
				new (*(internal::PtrWrapper*)(m_Data + i)) TYPE;
		}

		// Initialise with pre-allocated data
		CArray(void* data, unsigned int size)
			: m_Size(size)
			, m_Data((TYPE*)data)
			, m_Allocator(0)
		{
		}

		~CArray()
		{
			if (m_Allocator)
			{
				// Call the destructor on each element and free the allocated memory
				for (unsigned int i = 0; i < m_Size; i++)
					m_Data[i].TYPE::~TYPE();
				m_Allocator->Free(m_Data);
			}
		}

		// A shallow copy of each member in the array
		void shallow_copy(const CArray<TYPE>& rhs)
		{
			m_Size = rhs.m_Size;
			m_Data = rhs.m_Data;
			m_Allocator = rhs.m_Allocator;
		}

		void deep_copy(const CArray<TYPE>& rhs, IAllocator* allocator)
		{
			m_Allocator = allocator;
			internal::Assert(m_Allocator != 0);

			// Allocate and copy each entry
			m_Size = rhs.m_Size;
			m_Data = (TYPE*)m_Allocator->Alloc(m_Size * sizeof(TYPE));
			for (unsigned int i = 0; i < m_Size; i++)
				m_Data[i] = rhs.m_Data[i];
		}

		// Removes an element from the list without reallocating any memory
		// Causes the order of the entries in the list to change
		void unstable_remove(unsigned int index)
		{
			internal::Assert(index < m_Size);
			m_Data[index] = m_Data[m_Size - 1];
			m_Size--;
		}

		int size() const
		{
			return m_Size;
		}

		TYPE* data()
		{
			return m_Data;
		}
		const TYPE* data() const
		{
			return m_Data;
		}

		TYPE& operator [] (unsigned int index)
		{
			internal::Assert(index < m_Size);
			return m_Data[index];
		}
		const TYPE& operator [] (unsigned int index) const
		{
			internal::Assert(index < m_Size);
			return m_Data[index];
		}

		static unsigned int data_offset()
		{
			return (unsigned int)&(((CArray<TYPE>*)0)->m_Data);
		}

	private:
		// No need to implement if it's not used - private to ensure they don't get called by accident
		CArray(const CArray& rhs);
		CArray& operator= (const CArray& rhs);

		unsigned int m_Size;
		TYPE* m_Data;
		IAllocator* m_Allocator;
	};


	//
	// A simple file interface that the database loader will use. Clients must
	// implement this before they can load a reflection database.
	//
	struct IFile
	{
		// Type and size implied from destination type
		template <typename TYPE> bool Read(TYPE& dest)
		{
			return Read(&dest, sizeof(TYPE));
		}

		// Reads data into an array that has been already allocated
		template <typename TYPE> bool Read(CArray<TYPE>& dest)
		{
			return Read(dest.data(), dest.size() * sizeof(TYPE));
		}

		// Derived classes must implement just the read function, returning
		// true on success, false otherwise.
		virtual bool Read(void* dest, int size) = 0;
	};


	//
	// Represents the range [start, end) for iterating over an array
	//
	struct Range
	{
		Range() : first(0), last(0)
		{
		}

		int first;
		int last;
	};
}