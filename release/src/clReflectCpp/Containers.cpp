
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clcpp/Containers.h>
#include <clcpp/FunctionCall.h>


// Read iterator implementation for C-Arrays
class ArrayReadIterator : public clcpp::IReadIterator
{
public:
	ArrayReadIterator()
		: m_ArrayData(0)
		, m_ElementSize(0)
		, m_Position(0)
		, m_Size(0)
	{
	}

	void Initialise(const clcpp::Primitive* primitive, const void* container_object, clcpp::ReadIterator& storage)
	{
		clcpp::internal::Assert(primitive != 0);
		clcpp::internal::Assert(container_object != 0);

		// Ensure this is a field
		clcpp::internal::Assert(primitive->kind == clcpp::Primitive::KIND_FIELD);
		const clcpp::Field* field = (clcpp::Field*)primitive;

		// Describe the array value type
		m_ArrayData = (char*)container_object;
		storage.m_ValueType = field->type;
		storage.m_ValueIsPtr = field->qualifier.op == clcpp::Qualifier::POINTER;

		// Determine the size of an element
		m_ElementSize = storage.m_ValueType->size;
		if (storage.m_ValueIsPtr)
			m_ElementSize = sizeof(void*);

		// Prepare for iteration3
		m_Position = 0;
		storage.m_Count = field->ci->count;
		m_Size = storage.m_Count * m_ElementSize;
	}

	clcpp::ContainerKeyValue GetKeyValue() const
	{
		clcpp::internal::Assert(m_Position < m_Size);
		clcpp::ContainerKeyValue kv;
		kv.value = m_ArrayData + m_Position;
		return kv;
	}

	void MoveNext()
	{
		m_Position += m_ElementSize;
	}

private:
	// Construction values
	const char* m_ArrayData;
    clcpp::size_type m_ElementSize;

	// Iteration values
    clcpp::size_type m_Position;
    clcpp::size_type m_Size;
};


// Write iterator implementation for C-Arrays
class ArrayWriteIterator : public clcpp::IWriteIterator
{
public:
	ArrayWriteIterator()
		: m_ArrayData(0)
		, m_ElementSize(0)
		, m_Position(0)
		, m_Size(0)
	{
	}

	void Initialise(const clcpp::Primitive* primitive, void* container_object, clcpp::WriteIterator& storage, int count)
	{
		clcpp::internal::Assert(primitive != 0);
		clcpp::internal::Assert(container_object != 0);

		// Ensure this is a field
		clcpp::internal::Assert(primitive->kind == clcpp::Primitive::KIND_FIELD);
		const clcpp::Field* field = (clcpp::Field*)primitive;

		// Describe the array value type
		m_ArrayData = (char*)container_object;
		storage.m_ValueType = field->type;
		storage.m_ValueIsPtr = field->qualifier.op == clcpp::Qualifier::POINTER;

		// Determine the size of an element
		m_ElementSize = storage.m_ValueType->size;
		if (storage.m_ValueIsPtr)
			m_ElementSize = sizeof(void*);

		// Prepare for iteration
		m_Position = 0;
		storage.m_Count = count;
		m_Size = storage.m_Count * m_ElementSize;
	}

	void* AddEmpty()
	{
		clcpp::internal::Assert(m_Position < m_Size);
		void* value_ptr = m_ArrayData + m_Position;
		m_Position += m_ElementSize;
		return value_ptr;
	}

	void* AddEmpty(void* key)
	{
		return AddEmpty();
	}

private:
	// Construction values
	char* m_ArrayData;
    clcpp::size_type m_ElementSize;

	// Iteration values
    clcpp::size_type m_Position;
    clcpp::size_type m_Size;
};


clcpp::ReadIterator::ReadIterator(const TemplateType* type, const void* container_object)
{
	// Can't make a read iterator if there's no container interface
	if (type->ci == 0)
		return;

	// Get the read iterator type
	m_IteratorImplType = type->ci->read_iterator_type->AsClass();
	if (m_IteratorImplType->constructor == 0 || m_IteratorImplType->destructor == 0)
		return;

	// Construct the iterator in the local store
	clcpp::internal::Assert(m_IteratorImplType->size < sizeof(m_ImplData));
	CallFunction(m_IteratorImplType->constructor, (IReadIterator*)m_ImplData);

	// Complete implementation-specific initialisation
	((IReadIterator*)m_ImplData)->Initialise(type, container_object, *this);
}


clcpp::ReadIterator::ReadIterator(const Field* field, const void* container_object)
{
	// Can't make a read iterator if there's no container interface
	if (field->ci == 0)
		return;

	// Assume it's a C-Array
	clcpp::internal::CallConstructor((ArrayReadIterator*)m_ImplData);

	// Complete implementation-specific initialisation
	((IReadIterator*)m_ImplData)->Initialise(field, container_object, *this);
}


clcpp::ReadIterator::~ReadIterator()
{
	// Destruct the read iterator
	if (m_IteratorImplType != 0)
		CallFunction(m_IteratorImplType->destructor, (IReadIterator*)m_ImplData);
	else
		clcpp::internal::CallDestructor((ArrayReadIterator*)m_ImplData);
}


clcpp::WriteIterator::WriteIterator()
	: m_Initialised(false)
{
}


clcpp::WriteIterator::~WriteIterator()
{
	if (m_Initialised)
	{
		// Destruct the write iterator
		if (m_IteratorImplType != 0)
			CallFunction(m_IteratorImplType->destructor, (IWriteIterator*)m_ImplData);
		else
			clcpp::internal::CallDestructor((ArrayWriteIterator*)m_ImplData);
	}
}


void clcpp::WriteIterator::Initialise(const TemplateType* type, void* container_object, int count)
{
	// Can't make a write iterator if there's no container interface
	if (type->ci == 0)
		return;

	// Get the write iterator type
	m_IteratorImplType = type->ci->write_iterator_type->AsClass();
	if (m_IteratorImplType->constructor == 0 || m_IteratorImplType->destructor == 0)
		return;

	// Construct the iterator in the local store
	clcpp::internal::Assert(m_IteratorImplType->size < sizeof(m_ImplData));
	CallFunction(m_IteratorImplType->constructor, (IWriteIterator*)m_ImplData);

	// Complete implementation-specific initialisation
	((IWriteIterator*)m_ImplData)->Initialise(type, container_object, *this, count);
	m_Initialised = true;
}


void clcpp::WriteIterator::Initialise(const Field* field, void* container_object)
{
	// Can't make a write iterator if there's no container interface
	if (field->ci == 0)
		return;

	// Assume it's a C-Array
	clcpp::internal::CallConstructor((ArrayWriteIterator*)m_ImplData);

	// Complete implementation-specific initialisation
	((IWriteIterator*)m_ImplData)->Initialise(field, container_object, *this, field->ci->count);
	m_Initialised = true;
}


bool clcpp::WriteIterator::IsInitialised() const
{
	return m_Initialised;
}

