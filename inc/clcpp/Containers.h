
#pragma once


#include "clcpp.h"


// Partially reflect so that they can be used as base classes
clcpp_reflect_part(clcpp::IReadIterator)
clcpp_reflect_part(clcpp::IWriteIterator)


namespace clcpp
{
	class ReadIterator;
	class WriteIterator;


	//
	// Contains pointers to both the key and value objects in a container,
	// returned during read iteration. For containers with no keys, the
	// key pointer will always be null.
	//
	// Packing the objects into this structure saves an extra virtual call
	// per container value.
	//
	struct ContainerKeyValue
	{
		ContainerKeyValue()
			: key(0)
			, value(0)
		{
		}

		const void* key;
		const void* value;
	};


	//
	// The interface that the various read iterators for containers must
	// derive from.
	//
	struct IReadIterator
	{
		virtual ~IReadIterator() { }

		// One-time initialisation of the iterator that should initialise its own internal
		// values and write back what it knows of the container to ReadIterator.
		virtual void Initialise(const Primitive* primitive, const void* container_object, ReadIterator& storage) = 0;

		// Return the key/value pair at the current iterator position
		virtual ContainerKeyValue GetKeyValue() const = 0;

		// Move onto the next value in the container
		virtual void MoveNext() = 0;
	};


	//
	// The interface that the various write iterators for containers must
	// derive from.
	//
	struct IWriteIterator
	{
		virtual ~IWriteIterator() { }

		// One-time initialisation of the iterator that should initialise its own internal
		// values and write back what it knows of the container to WriteIterator. The total
		// count of values you expect to write to the container needs to be passed as a
		// parameter.
		virtual void Initialise(const Primitive* primitive, void* container_object, unsigned int count, WriteIterator& storage) = 0;

		// Allocate an empty value in the container at the current iterator position and return
		// a pointer to that value so that it can be written to. Moves onto the next value after
		// the call.
		virtual void* AddEmpty() = 0;

		// Allocate an empty value with the given key object at the current iterator position
		// and return a pointer to that value so that it can be written to. Moves onto the next
		// value after the call.
		virtual void* AddEmpty(void* key) = 0;
	};


	//
	// The base class for the runtime read/write iterator wrappers. A small amount of memory
	// is allocated on the stack, within which the required read/write iterator implementation
	// is constructed, negating the need for runtime memory allocation.
	//
	// This also stores general information useful at runtime for iterating over a container.
	//
	struct Iterator
	{
		Iterator()
			: m_Count(0)
			, m_KeyType(0)
			, m_ValueType(0)
			, m_KeyIsPtr(false)
			, m_ValueIsPtr(false)
			, m_IteratorImplType(0)
		{
		}

		unsigned int m_Count;
		const Type* m_KeyType;
		const Type* m_ValueType;
		bool m_KeyIsPtr;
		bool m_ValueIsPtr;

	protected:
		char m_ImplData[128];
		const Class* m_IteratorImplType;
	};


	//
	// Read iterator implementation wrapper
	//
	class ReadIterator : public Iterator
	{
	public:
		// Construct from a template type
		ReadIterator(const TemplateType* type, const void* container_object);

		// Construct from a field; can only be used to construct read iterators for
		// C-Array fields.
		ReadIterator(const Field* field, const void* container_object);

		~ReadIterator();

		// Calls directly into the iterator implementation
		ContainerKeyValue GetKeyValue() const
		{
			return ((IReadIterator*)m_ImplData)->GetKeyValue();
		}
		void MoveNext()
		{
			((IReadIterator*)m_ImplData)->MoveNext();
		}
	};


	//
	// Write iterator implementation wrapper
	//
	class WriteIterator : public Iterator
	{
	public:
		// Construct from a template type
		WriteIterator(const TemplateType* type, void* container_object, unsigned int count);

		// Construct from a field; can only be used to construct write iterators for
		// C-Array fields.
		WriteIterator(const Field* field, void* container_object);

		~WriteIterator();

		// Calls directly into the iterator implementation
		void* AddEmpty()
		{
			return ((IWriteIterator*)m_ImplData)->AddEmpty();
		}
		void* AddEmpty(void* key)
		{
			return ((IWriteIterator*)m_ImplData)->AddEmpty(key);
		}
	};
}