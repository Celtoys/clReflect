
#pragma once


#include "clcpp.h"


// Partially reflect so that they can be used as base classes
clcpp_reflect_part(clcpp::IReadIterator)
clcpp_reflect_part(clcpp::IWriteIterator)


namespace clcpp
{
	class ReadIterator;


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
	// Read-only iteration over a container
	//
	struct IReadIterator
	{
		virtual ~IReadIterator() { }

		// Reading interface
		virtual void Initialise(const void* container_object, const TemplateType* template_type, ReadIterator& storage) = 0;
		virtual ContainerKeyValue GetKeyValue() const = 0;
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


	class ReadIterator
	{
	public:
		unsigned int m_Count;
		const Type* m_KeyType;
		const Type* m_ValueType;
		bool m_KeyIsPtr;
		bool m_ValueIsPtr;

		ReadIterator(const TemplateType* type, const void* container_object);
		~ReadIterator();

		ContainerKeyValue GetKeyValue() const
		{
			internal::Assert(m_ReaderType != 0);
			return ((IReadIterator*)m_ImplData)->GetKeyValue();
		}

		void MoveNext()
		{
			internal::Assert(m_ReaderType != 0);
			((IReadIterator*)m_ImplData)->MoveNext();
		}

	private:
		char m_ImplData[128];
		const Class* m_ReaderType;
	};
}