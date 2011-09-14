
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

#include <clutl/Containers.h>
#include <clcpp/Core.h>

#include <string.h>


clutl::DataBuffer::DataBuffer(unsigned int capacity)
	: m_Data(0)
	, m_Capacity(capacity)
	, m_Size(0)
	, m_Position(0)
{
	m_Data = new char[m_Capacity];
}


clutl::DataBuffer::~DataBuffer()
{
	delete [] m_Data;
}


void clutl::DataBuffer::ResetPosition()
{
	m_Position = 0;
}


void clutl::DataBuffer::ResetPositionAndSize()
{
	m_Position = 0;
	m_Size = 0;
}


void clutl::DataBuffer::Write(const void* data, unsigned int length)
{
	// Append to the internal buffer
	unsigned int new_pos = m_Position + length;
	clcpp::internal::Assert(new_pos <= m_Capacity && "Write overflow");
	memcpy(m_Data + m_Position, data, length);
	m_Position = new_pos;

	// Ensure the size is always updated - position might have been reset between writes
	if (new_pos > m_Size)
		m_Size = new_pos;
}


void clutl::DataBuffer::WriteAt(const void* data, unsigned int length, unsigned int position)
{
	// Overwrite an existing location in the buffer
	unsigned int new_size = position + length;
	clcpp::internal::Assert(new_size <= m_Capacity && "Write overflow");
	memcpy(m_Data + position, data, length);

	// Ensure the size is always updated - position might have been reset between writes
	if (new_size > m_Size)
		m_Size = new_size;
}


void clutl::DataBuffer::Read(void* data, unsigned int length)
{
	// Copy from the buffer and move on length bytes
	clcpp::internal::Assert(m_Position + length <= m_Size && "Read overflow");
	memcpy(data, m_Data + m_Position, length);
	m_Position += length;
}


const char* clutl::DataBuffer::ReadAt(unsigned int position) const
{
	clcpp::internal::Assert(position <= m_Size && "Read overflow");
	return m_Data + position;
}


void clutl::DataBuffer::SeekAbs(unsigned int position)
{
	clcpp::internal::Assert(position <= m_Size && "Seek overflow");
	m_Position = position;	
}


void clutl::DataBuffer::SeekRel(int offset)
{
	clcpp::internal::Assert(m_Position + offset <= m_Size && "Seek overflow");
	m_Position += offset;	
}


void clutl::DataBuffer::SeekEnd(int offset)
{
	m_Position -= offset;
	clcpp::internal::Assert(m_Position <= m_Size && "Seek overflow");
}