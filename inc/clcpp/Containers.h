
#pragma once


#include "clcpp.h"


// Partially reflect so that they can be used as base classes
clcpp_reflect_part(clcpp::IReadIterator)
clcpp_reflect_part(clcpp::IWriteIterator)


namespace clcpp
{
	//
	// Read-only iteration over a container
	//
	struct IReadIterator
	{
		virtual ~IReadIterator() { }

		// Reading interface
		virtual void Initialise(const void* container_object, const Type* value_type, const Type* key_type) = 0;
		virtual const void* GetKey() const = 0;
		virtual const void* GetValue() const = 0;
		virtual unsigned int GetCount() const = 0;
		virtual void MoveNext() = 0;
	};


	//
	// Need to pre-allocate the number of elements
	// Can then call the constructor on each element
	// Low-level write access to any container
	//
	struct IWriteIterator
	{
		virtual ~IWriteIterator() { }

		// Construction interface
		virtual void Initialise(void* container_object, const Type* value_type, const Type* key_type) = 0;
		virtual void SetCount(unsigned int count) = 0;
		virtual void* AddEmpty() = 0;
		virtual void* AddEmpty(void* key) = 0;
	};
}