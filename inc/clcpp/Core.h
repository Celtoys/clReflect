
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


// Unified platform determining interface

// checking for compiler type
#if defined(_MSC_VER)
#define CLCPP_USING_MSVC
#else
#define CLCPP_USING_GNUC

#if defined(__APPLE__)
#define CLCPP_USING_GNUC_MAC
#endif // __APPLE__

#endif // _MSC_VER

// checking if we are running on 32 bit or 64 bit
#if defined(CLCPP_USING_MSVC)

// checking for windows platform
#if defined(_WIN64)
#define CLCPP_USING_64_BIT
#else
#define CLCPP_USING_32_BIT
#endif // _WIN64

#else

// checking for g++ and clang
#if defined(__LP64__)
#define CLCPP_USING_64_BIT
#else
#define CLCPP_USING_32_BIT
#endif // __LP64__

#endif // CLCPP_USING_MSVC


namespace clcpp
{
    // Defining cross platform size type and pointer type
    //
    // TL;DR version: use pointer_type to holding value casted from a pointer
    // use size_type to hold memory index, offset, length, etc.
    //
    //
    // size_type is a type that can hold any array index, i.e. size_type
    // is used to hold size of a memory block. It is also used to hold the
    // offset of one address from a base address. Remember size_type is an
    // unsigned type, so it only can hold positive offsets.
    //
    // pointer_type is a type for holding address value casting from a pointer.
    //
    // In C99, size_type is exactly size_t, and pointer_type is exactly intptr_t
    // (or uintptr_t), we provide a different name and put it in clcpp here
    // so as not to pollute default namespace.
    //
    // Although the actual types here are the same, the c standard
    // does not enforce these two data to be the same. So we separate them in case
    // we met some platforms fof which these two have different type widths.
    //
    // Since all offsets used in clReflect are positive offsets from a base
    // address, we do not provide ptrdiff_t type here for simplicity. Instead
    // we merge the use cases of ptrdiff_t into size_type(comparing
    // a negative ptrdiff_t type variable with a size_t will be a disaster).
    #if defined(CLCPP_USING_64_BIT)

        typedef unsigned long size_type;
        typedef unsigned long pointer_type;

    #else

        typedef unsigned int size_type;
        typedef unsigned int pointer_type;

    #endif // CLCPP_USING_64_BIT

	// cross platform type definitions
	#if defined(CLCPP_USING_MSVC)

		typedef __int64 int64;
		typedef unsigned __int64 uint64;
        typedef unsigned __int32 uint32;

    #else

		typedef long long int64;
		typedef unsigned long long uint64;
        typedef unsigned int uint32;

	#endif // _MSC_VER

    #if defined(CLCPP_USING_GNUC)
        // When we patch GetType and GetTypeNameHash functions, we first search for
        // specific mov instructions, and when we found them, we would read the value
        // at the address calculated from the instruction. If the value equals the
        // identifier here, we would assume we find the location to patch.
        // This would require the following value will not be identical with any
        // other valid address used. That's why we use odd-ended values here, hoping
        // memory alignment will help us reduce the chance of being the same with
        // other addresses.

    #define CLCPP_INVALID_HASH (0xfefe012f)

    #if defined(CLCPP_USING_64_BIT)
    #define CLCPP_INVALID_ADDRESS (0xffee01ef12349007)
    #else
    #define CLCPP_INVALID_ADDRESS (0xffee6753)
    #endif // CLCPP_USING_64_BIT

    #endif // CLCPP_USING_GNUC

	namespace internal
	{
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
		struct PtrWrapper
		{
		};
	}
}

//
// Placement new for the PtrWrapper logic specified above, which required matching delete
//
inline void* operator new (clcpp::size_type size, const clcpp::internal::PtrWrapper& p)
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
#ifdef CLCPP_USING_MSVC
				__asm
				{
					int 3h
				}
#else
                asm("int $0x3\n");
#endif // CLCPP_USING_MSVC
			}
		}


		//
		// Functions to abstract the calling of an object's constructor and destructor, for
		// debugging and letting the compiler do the type deduction. Makes it a little easier
		// to use the PtrWrapper abstraction.
		//
		template <typename TYPE>
		inline void CallConstructor(TYPE* object)
		{
			new (*(PtrWrapper*)object) TYPE;
		}
		template <typename TYPE>
		inline void CallDestructor(TYPE* object)
		{
			object->~TYPE();
		}


		//
		// Hashes the specified data into a 32-bit value
		//
		unsigned HashData(const void* data, int length, unsigned int seed = 0);


		//
		// Hashes the full string into a 32-bit value
		//
		unsigned int HashNameString(const char* name_string, unsigned int seed = 0);


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
		virtual void* Alloc(size_type size) = 0;
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
				internal::CallConstructor(m_Data + i);
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
					internal::CallDestructor(m_Data + i);
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

        // TODO: This size is actually count here, so we may not need to convert it to size_type?
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
		virtual bool Read(void* dest, size_type size) = 0;
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


