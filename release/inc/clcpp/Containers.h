
#pragma once


#include "clcpp.h"


clcpp_reflect_part(clcpp)
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
	clcpp_attr(reflect_part)
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
	clcpp_attr(reflect_part)
	struct IWriteIterator
	{
		virtual ~IWriteIterator() { }

		// One-time initialisation of the iterator that should initialise its own internal
		// values and write back what it knows of the container to WriteIterator. Use the count
		// parameter to pre-allocate all the values that need writing.
		virtual void Initialise(const Primitive* primitive, void* container_object, WriteIterator& storage, int count) = 0;

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
		WriteIterator();
		~WriteIterator();

		// Construct from a template type with the number of elements you're going to write
		void Initialise(const TemplateType* type, void* container_object, int count);

		// Construct from a field; can only be used to construct write iterators for
		// C-Array fields.
		void Initialise(const Field* field, void* container_object);

		bool IsInitialised() const;

		// Calls directly into the iterator implementation
		void* AddEmpty()
		{
			return ((IWriteIterator*)m_ImplData)->AddEmpty();
		}
		void* AddEmpty(void* key)
		{
			return ((IWriteIterator*)m_ImplData)->AddEmpty(key);
		}

	private:
		bool m_Initialised;
	};
}