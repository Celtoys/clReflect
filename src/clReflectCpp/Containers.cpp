
#include <clcpp/Containers.h>


clcpp::ReadIterator::ReadIterator(const TemplateType* type, const void* container_object)
	: m_Count(0)
	, m_KeyType(0)
	, m_ValueType(0)
	, m_KeyIsPtr(false)
	, m_ValueIsPtr(false)
	, m_ReaderType(0)
{
	// Can't make a read iterator if there's no container interface
	if (type->ci == 0)
		return;

	// Get the read iterator type
	m_ReaderType = type->ci->read_iterator_type->AsClass();
	if (m_ReaderType->constructor == 0 || m_ReaderType->destructor == 0)
		return;

	// Construct the iterator in the local store
	clcpp::internal::Assert(m_ReaderType->size < sizeof(m_ImplData));
	CallFunction(m_ReaderType->constructor, (IReadIterator*)m_ImplData);

	// Complete implementation-specific initialisation
	((IReadIterator*)m_ImplData)->Initialise(container_object, type, *this);
}


clcpp::ReadIterator::~ReadIterator()
{
	// Destruct the read iterator
	if (m_ReaderType != 0)
		CallFunction(m_ReaderType->destructor, (IReadIterator*)m_ImplData);
}