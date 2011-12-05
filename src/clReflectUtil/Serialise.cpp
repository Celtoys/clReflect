
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011 Don Williamson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ===============================================================================
//

#include <clutl/Serialise.h>
#include <clcpp/Core.h>


// Explicit dependency
// TODO: Some how remove the need for this or provide a means of locating it on the target platform
extern "C" void* __cdecl memcpy(void* dst, const void* src, unsigned int size);


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


void clutl::ObjectList::AddObject(Object* object)
{
	Object** dest = (Object**)m_Data.Alloc(sizeof(Object*));
	*dest = object;
}
