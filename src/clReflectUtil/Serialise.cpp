
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clutl/Serialise.h>


// Standard C library function, copy bytes
// http://pubs.opengroup.org/onlinepubs/009695399/functions/memcpy.html

#ifdef __GNUC__
	#define __THROW	throw ()
	#define __nonnull(params) __attribute__ ((__nonnull__ params))
#else
	#define __THROW
	#define __nonnull(params)
#endif

extern "C" void* CLCPP_CDECL memcpy(void* dst, const void* src, clcpp::size_type size) __THROW __nonnull ((1, 2));


clutl::WriteBuffer::WriteBuffer()
	: m_Data(0)
	, m_DataEnd(0)
	, m_DataWrite(0)
{
	// Use a default capacity to prevent Write having to do too much checking
	unsigned int default_capacity = 32;
	m_Data = new char[default_capacity];
	m_DataEnd = m_Data + default_capacity;
	m_DataWrite = m_Data;
}


clutl::WriteBuffer::WriteBuffer(unsigned int initial_capacity)
	: m_Data(0)
	, m_DataEnd(0)
	, m_DataWrite(0)
{
	// Allocate initial capacity
	m_Data = new char[initial_capacity];
	m_DataEnd = m_Data + initial_capacity;
	m_DataWrite = m_Data;
}


clutl::WriteBuffer::~WriteBuffer()
{
	if (m_Data != 0)
		delete [] m_Data;
}


void clutl::WriteBuffer::Reset()
{
	m_DataWrite = m_Data;
}


void* clutl::WriteBuffer::Alloc(unsigned int length)
{
	// TODO: On platforms that support virtual memory, copy can be eliminated
	if (m_DataWrite + length > m_DataEnd)
	{
		// Repeatedly calculate a new capacity of 1.5x until the new data fits
		unsigned int new_capacity = m_DataEnd - m_Data;
		unsigned int write_pos = m_DataWrite - m_Data;
		while (write_pos + length > new_capacity)
			new_capacity += new_capacity / 2;

		// Allocate the new data and copy over
		char* new_data = new char[new_capacity];
		memcpy(new_data, m_Data, m_DataWrite - m_Data);
		delete [] m_Data;

		// Swap in the new buffer
		m_Data = new_data;
		m_DataEnd = m_Data + new_capacity;
		m_DataWrite = m_Data + write_pos;
	}

	// Advance the write pointer by the desired amount
	void* data_write = m_DataWrite;
	m_DataWrite += length;
	return data_write;
}


void clutl::WriteBuffer::Write(const void* data, unsigned int length)
{
	// Allocate enough space for the data and copy it
	void* data_write = Alloc(length);
	memcpy(data_write, data, length);
}


void clutl::WriteBuffer::WriteStr(const char* str)
{
	// Calculate string length before writing the data
	const char* end = str;
	while (*end)
		end++;
	Write(str, end - str);
}


void clutl::WriteBuffer::WriteChar(char c)
{
	char* data_write = (char*)Alloc(1);
	*data_write = c;
}


void clutl::WriteBuffer::SeekRel(int offset)
{
	clcpp::internal::Assert(m_DataWrite + offset <= m_DataEnd && "Seek overflow");
	m_DataWrite += offset;
}


clutl::ReadBuffer::ReadBuffer(const WriteBuffer& write_buffer)
	: m_Data(write_buffer.GetData())
	, m_DataEnd(write_buffer.GetData() + write_buffer.GetBytesWritten())
	, m_DataRead(write_buffer.GetData())
{
}


void clutl::ReadBuffer::Read(void* data, unsigned int length)
{
	// Copy from the buffer and move on length bytes
	clcpp::internal::Assert(m_DataRead + length <= m_DataEnd && "Read overflow");
	memcpy(data, m_DataRead, length);
	m_DataRead += length;
}


const char* clutl::ReadBuffer::ReadAt(unsigned int position) const
{
	clcpp::internal::Assert(m_Data + position <= m_DataEnd && "Read overflow");
	return m_Data + position;
}


void clutl::ReadBuffer::SeekRel(int offset)
{
	clcpp::internal::Assert(m_DataRead + offset <= m_DataEnd && "Seek overflow");
	m_DataRead += offset;
}
