#pragma once


#include <clcpp/clcpp.h>


//
// Embellish the implementation of clcpp::CArray with some RAII functionality
//
template <typename TYPE>
class CArray : public clcpp::CArray<TYPE>
{
public:
	// Initialise an empty array
	CArray()
	{
		this->size = 0;
		this->data = 0;
		this->allocator = 0;
	}

	// Initialise with array count and allocator
	CArray(unsigned int _size, clcpp::IAllocator* _allocator)
	{		
		this->size = _size;
		this->allocator = _allocator;

		// Allocate and call the constructor for each element
		this->data = (TYPE*)this->allocator->Alloc(this->size * sizeof(TYPE));
		for (unsigned int i = 0; i < this->size; i++)
			clcpp::internal::CallConstructor(this->data + i);
	}

	// Initialise with pre-allocated data
	CArray(TYPE* _data, unsigned int _size)
	{
		this->size = _size;
		this->data = _data;
		this->allocator = 0;
	}

	~CArray()
	{
		if (this->allocator)
		{
			// Call the destructor on each element and free the allocated memory
			for (unsigned int i = 0; i < this->size; i++)
				clcpp::internal::CallDestructor(this->data + i);
			this->allocator->Free(this->data);
		}
	}

	TYPE& operator [] (unsigned int index)
	{
		clcpp::internal::Assert(index < this->size);
		return this->data[index];
	}
	const TYPE& operator [] (unsigned int index) const
	{
		clcpp::internal::Assert(index < this->size);
		return this->data[index];
	}
};


//
// Procedural operations on the clcpp::CArray type
//


inline clcpp::size_type array_data_offset()
{
	#if defined(CLCPP_USING_MSVC)
	    return (clcpp::size_type) (&(((clcpp::CArray<int>*)0)->data));
	#else
		// GCC does not support applying offsetof on non-POD types
		clcpp::CArray<int> dummy;
		return ((clcpp::size_type) (&(dummy.data))) - ((clcpp::size_type) (&dummy));
	#endif	// CLCPP_USING_MSVC
}


template <typename TYPE>
inline void shallow_copy(clcpp::CArray<TYPE>& dest, const clcpp::CArray<TYPE>& src)
{
	dest.size = src.size;
	dest.data = src.data;
	dest.allocator = src.allocator;
}


template <typename TYPE>
inline void unstable_remove(clcpp::CArray<TYPE>& array, unsigned int index)
{
	// Removes an element from the list without reallocating any memory
	// Causes the order of the entries in the list to change
	clcpp::internal::Assert(index < array.size);
	array.data[index] = array.data[array.size - 1];
	array.size--;
}


template <typename TYPE>
inline void stable_remove(clcpp::CArray<TYPE>& array, unsigned int index)
{
	// Removes an element from the list without reallocating any memory
	// Maintains array element order by copying from the end
	clcpp::internal::Assert(index < array.size);
	for (unsigned int i = index; i < array.size - 1; i++)
		array.data[i] = array.data[i + 1];
	array.size--;
}


template <typename TYPE>
inline void deep_copy(clcpp::CArray<TYPE>& dest, const clcpp::CArray<TYPE>& src, clcpp::IAllocator* allocator)
{
	allocator = allocator;
	clcpp::internal::Assert(allocator != 0);

	// Allocate and copy each entry
	dest.size = src.size;
	dest.data = (TYPE*)allocator->Alloc(dest.size * sizeof(TYPE));
	for (unsigned int i = 0; i < dest.size; i++)
		dest.data[i] = src.data[i];
}
