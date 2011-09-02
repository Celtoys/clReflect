
#include <clutl/Containers.h>
#include <clcpp/Core.h>

#include <string.h>


clutl::OutputBuffer::OutputBuffer(unsigned int size)
	: m_Data(0)
	, m_Size(size)
	, m_Position(0)
{
	m_Data = new char[m_Size];
}


clutl::OutputBuffer::~OutputBuffer()
{
	delete [] m_Data;
}


void clutl::OutputBuffer::Write(const void* data, unsigned int length)
{
	// Append to the internal buffer
	clcpp::internal::Assert(m_Position + length <= m_Size && "Buffer overflow");
	memcpy(m_Data + m_Position, data, length);
	m_Position += length;
}


void clutl::OutputBuffer::WriteAt(const void* data, unsigned int length, unsigned int position)
{
	// Overwrite an existing location in the buffer
	clcpp::internal::Assert(position + length <= m_Size && "Buffer overflow");
	memcpy(m_Data + position, data, length);
}