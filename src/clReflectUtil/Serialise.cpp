
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
    #define __THROW throw()
    #define __nonnull(params) __attribute__((__nonnull__ params))
#else
    #define __THROW
    #define __nonnull(params)
#endif

extern "C" void* CLCPP_CDECL memcpy(void* dst, const void* src, clcpp::size_type size) __THROW __nonnull((1, 2));

clutl::WriteBuffer::WriteBuffer(unsigned int initial_capacity)
{
    // Allocate initial capacity
    m_data = new char[initial_capacity];
    m_dataEnd = m_data + initial_capacity;
    m_dataWrite = m_data;
}

clutl::WriteBuffer::~WriteBuffer()
{
    delete[] m_data;
}

void clutl::WriteBuffer::Reset()
{
    m_dataWrite = m_data;
}

void* clutl::WriteBuffer::Alloc(unsigned int length)
{
    // TODO: On platforms that support virtual memory, copy can be eliminated
    if (m_dataWrite + length > m_dataEnd)
    {
        // Repeatedly calculate a new capacity of 1.5x until the new data fits
        unsigned int new_capacity = (m_data == nullptr) ? 32 : m_dataEnd - m_data;
        unsigned int write_pos = m_dataWrite - m_data;
        while (write_pos + length > new_capacity)
        {
            new_capacity += new_capacity / 2;
        }

        // Allocate the new data and copy over
        char* new_data = new char[new_capacity];
        if (m_data != nullptr)
        {
            memcpy(new_data, m_data, m_dataWrite - m_data);
            delete[] m_data;
        }

        // Swap in the new buffer
        m_data = new_data;
        m_dataEnd = m_data + new_capacity;
        m_dataWrite = m_data + write_pos;
    }

    // Advance the write pointer by the desired amount
    void* data_write = m_dataWrite;
    m_dataWrite += length;
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
    while (*end != 0)
    {
        end++;
    }
    Write(str, end - str);
}

void clutl::WriteBuffer::WriteChar(char c)
{
    auto* data_write = static_cast<char*>(Alloc(1));
    *data_write = c;
}

void clutl::WriteBuffer::SeekRel(int offset)
{
    char* data_write = m_dataWrite + offset;
    clcpp::internal::Assert(data_write >= m_data && "Seek underflow");
    clcpp::internal::Assert(data_write <= m_dataEnd && "Seek overflow");
    m_dataWrite = data_write;
}

clutl::ReadBuffer::ReadBuffer(const WriteBuffer& write_buffer)
    : m_data(write_buffer.GetData())
    , m_dataEnd(write_buffer.GetData() + write_buffer.GetBytesWritten())
    , m_dataRead(write_buffer.GetData())
{
}

clutl::ReadBuffer::ReadBuffer(const void* data, unsigned int length)
    : m_data(static_cast<const char*>(data))
    , m_dataEnd(m_data + length)
    , m_dataRead(m_data)
{
}

void clutl::ReadBuffer::Read(void* data, unsigned int length)
{
    // Copy from the buffer and move on length bytes
    clcpp::internal::Assert(m_dataRead + length <= m_dataEnd && "Read overflow");
    memcpy(data, m_dataRead, length);
    m_dataRead += length;
}

const char* clutl::ReadBuffer::ReadAt(unsigned int position) const
{
    clcpp::internal::Assert(m_data + position <= m_dataEnd && "Read overflow");
    return m_data + position;
}

void clutl::ReadBuffer::SeekRel(int offset)
{
    clcpp::internal::Assert(m_dataRead + offset <= m_dataEnd && "Seek overflow");
    m_dataRead += offset;
}
