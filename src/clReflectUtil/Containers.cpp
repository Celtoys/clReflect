
#include <clutl/Containers.h>
#include <clcpp/Core.h>

#include <string.h>


clutl::DataBuffer::DataBuffer(unsigned int size)
	: m_Data(0)
	, m_Size(size)
	, m_Position(0)
{
	m_Data = new char[m_Size];
}


clutl::DataBuffer::~DataBuffer()
{
	delete [] m_Data;
}


void clutl::DataBuffer::Reset()
{
	m_Position = 0;
}


void clutl::DataBuffer::Write(const void* data, unsigned int length)
{
	// Append to the internal buffer
	clcpp::internal::Assert(m_Position + length <= m_Size && "Buffer overflow");
	memcpy(m_Data + m_Position, data, length);
	m_Position += length;
}


void clutl::DataBuffer::WriteAt(const void* data, unsigned int length, unsigned int position)
{
	// Overwrite an existing location in the buffer
	clcpp::internal::Assert(position + length <= m_Size && "Buffer overflow");
	memcpy(m_Data + position, data, length);
}


void clutl::DataBuffer::Read(void* data, unsigned int length)
{
	// Copy from the buffer and move on length bytes
	clcpp::internal::Assert(m_Position + length <= m_Size && "Buffer overflow");
	memcpy(data, m_Data + m_Position, length);
	m_Position += length;
}


const char* clutl::DataBuffer::ReadAt(unsigned int position) const
{
	clcpp::internal::Assert(position <= m_Size && "Buffer overflow");
	return m_Data + position;
}


void clutl::DataBuffer::SeekAbs(unsigned int position)
{
	clcpp::internal::Assert(position <= m_Size && "Buffer overflow");
	m_Position = position;	
}


void clutl::DataBuffer::SeekRel(int offset)
{
	clcpp::internal::Assert(m_Position + offset <= m_Size && "Buffer overflow");
	m_Position += offset;	
}


void clutl::DataBuffer::SeekEnd(int offset)
{
	m_Position -= offset;
	clcpp::internal::Assert(m_Position <= m_Size && "Buffer overflow");
}