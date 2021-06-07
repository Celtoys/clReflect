
//
// ===============================================================================
// clReflect, Serialise.h - All different types of serialisation that clReflect
// supports.
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once

#include <clcpp/clcpp.h>

clcpp_reflect_part(clutl);
namespace clutl
{
    struct Object;
    class JSONContext;

    //
    // Growable write byte buffer
    //
    class CLCPP_API attrReflect WriteBuffer
    {
    public:
        WriteBuffer() = default;
        WriteBuffer(unsigned int initial_capacity);
        ~WriteBuffer();

        // Resets only the write position, ensuring none of the capacity already allocated is released
        void Reset();

        // Allocate space in the buffer, shifting the write position and returning a pointer to that space
        // Grows the capacity on demand
        void* Alloc(unsigned int length);

        // Copy data into the write buffer
        // Grows the capacity on demand
        void Write(const void* data, unsigned int length);

        // Utilities built upon Alloc/Write
        void WriteStr(const char* str);
        void WriteChar(char c);

        void SeekRel(int offset);

        const char* GetData() const
        {
            return m_data;
        }
        unsigned int GetBytesWritten() const
        {
            return m_dataWrite - m_data;
        }
        unsigned int GetBytesAllocated() const
        {
            return m_dataEnd - m_data;
        }

    private:
        // Disable copying
        WriteBuffer(const WriteBuffer&);
        WriteBuffer& operator=(const WriteBuffer&);

        char* m_data = nullptr;
        char* m_dataEnd = nullptr;
        char* m_dataWrite = nullptr;
    };

    //
    // Lightweight read buffer that uses the contents of a write buffer that must exist
    // for the life time of this read buffer.
    //
    class CLCPP_API attrReflect ReadBuffer
    {
    public:
        ReadBuffer() = default;
        ReadBuffer(const WriteBuffer& write_buffer);
        ReadBuffer(const void* data, unsigned int length);

        // TODO: Not entirely convinced by this API with regards to the ability of its users
        //  to quickly, safely and easily detect buffer overflow scenarios before it asserts.
        void Read(void* data, unsigned int length);
        const char* ReadAt(unsigned int position) const;
        void SeekRel(int offset);

        unsigned int GetBytesRead() const
        {
            return m_dataRead - m_data;
        }
        unsigned int GetTotalBytes() const
        {
            return m_dataEnd - m_data;
        }
        unsigned int GetBytesRemaining() const
        {
            return m_dataEnd - m_dataRead;
        }

    private:
        const char* m_data = nullptr;
        const char* m_dataEnd = nullptr;
        const char* m_dataRead = nullptr;
    };

    struct IPtrMap
    {
        virtual bool CanMapPtr(const void* ptr, const clcpp::Type* type) = 0;
        virtual unsigned int MapPtr(const void* ptr) = 0;
    };

    // Binary serialisation
    CLCPP_API void SaveVersionedBinary(WriteBuffer& out, const void* object, const clcpp::Type* type);
    CLCPP_API void LoadVersionedBinary(ReadBuffer& in, void* object, const clcpp::Type* type);

    struct CLCPP_API JSONError
    {
        enum Code
        {
            NONE,
            UNEXPECTED_END_OF_DATA,
            EXPECTING_HEX_DIGIT,
            EXPECTING_DIGIT,
            UNEXPECTED_CHARACTER,
            INVALID_KEYWORD,
            INVALID_ESCAPE_SEQUENCE,
            UNEXPECTED_TOKEN,
        };

        Code code = NONE;

        // Position in the data buffer where the error occurred
        unsigned int position = 0;

        // An attempt to specify the exact line/column where the error occurred
        // Assuming the data buffer is reasonably formatted
        unsigned int line = 0;
        unsigned int column = 0;
    };

    struct JSONFlags
    {
        enum
        {
            INDENT_MASK = 0x0F,
            FORMAT_OUTPUT = 0x10,
            EMIT_HEX_FLOATS = 0x20,

            // Serialising pointer hashes in hexadecimal is more compact than decimal, however it's not compliant
            // with the JSON standard.
            EMIT_HEX_POINTERS = 0x40,

            // When saving class fields, default behaviour is to save them in the order that they appear in the class
            // field array. This array is typically sorted in order of name hash so that look-up by name can use
            // a binary search.
            //
            // This flag will ensure fields are saved in the order that they are declared by sorting them by their
            // byte offset first.
            //
            // Note that use of this flag will slow serialisation as the inner loop will have to do loop quadratically
            // over the field array.
            SORT_CLASS_FIELDS_BY_OFFSET = 0x80,
        };
    };

    // JSON serialisation
    CLCPP_API JSONError LoadJSON(ReadBuffer& in, void* object, const clcpp::Type* type, unsigned int transient_flags);
    CLCPP_API JSONError LoadJSON(JSONContext& ctx, void* object, const clcpp::Field* field, unsigned int transient_flags);

    // Save an object of a given type to the write buffer.
    // If ptr_save is null, no pointers are serialised.
    CLCPP_API void SaveJSON(WriteBuffer& out, const void* object, const clcpp::Type* type, IPtrMap* ptr_map, unsigned int flags,
                            unsigned int transient_flags);

    // Save an object described by the given field to the write buffer.
    // If ptr_save is null, no pointers are serialised.
    CLCPP_API void SaveJSON(WriteBuffer& out, const void* object, const clcpp::Field* field, IPtrMap* ptr_map, unsigned int flags,
                            unsigned int transient_flags);
}