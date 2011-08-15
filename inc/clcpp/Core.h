
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
		// Hashes the full string int a 32-bit value
		//
		unsigned int HashNameString(const char* name_string);


		//
		// Combines two hashes by using the first one as a seed and hashing the second one
		//
		unsigned int MixHashes(unsigned int a, unsigned int b);
	}


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
			, m_Owner(1)
		{
		}

		// Initialise with array count
		CArray(unsigned int size)
			: m_Size(size)
			, m_Data(new TYPE[size])
			, m_Owner(1)
		{
		}

		// Initialise with pre-allocated data
		CArray(void* data, unsigned int size)
			: m_Size(size)
			, m_Data((TYPE*)data)
			, m_Owner(0)
		{
		}

		// Copy construct
		CArray(const CArray& rhs)
			: m_Size(rhs.size())
			, m_Data(new TYPE[rhs.size()])
			, m_Owner(1)
		{
			// Copy each entry
			for (unsigned int i = 0; i < m_Size; i++)
			{
				m_Data[i] = rhs.m_Data[i];
			}
		}

		~CArray()
		{
			if (m_Owner)
			{
				delete [] m_Data;
			}
		}

		// A shallow copy of each member in the array
		void copy(const CArray<TYPE>& rhs)
		{
			m_Size = rhs.m_Size;
			m_Data = rhs.m_Data;
			m_Owner = rhs.m_Owner;
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

		CArray& operator = (const CArray& rhs)
		{
			// Check for self-assignment
			if (this == &rhs)
			{
				return *this;
			}

			// Default construct the array data
			delete [] m_Data;
			m_Size = rhs.m_Size;
			m_Data = new TYPE[m_Size];

			// Assign each entry
			for (unsigned int i = 0; i < m_Size; i++)
			{
				m_Data[i] = rhs.m_Data[i];
			}

			return *this;
		}

		static unsigned int data_offset()
		{
			return (unsigned int)&(((CArray<TYPE>*)0)->m_Data);
		}

	private:
		unsigned int m_Size : 31;
		unsigned int m_Owner : 1;
		TYPE* m_Data;
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
}