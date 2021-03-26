
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
		WriteBuffer();
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

		const char* GetData() const { return m_Data; }
		unsigned int GetBytesWritten() const { return m_DataWrite - m_Data; }
		unsigned int GetBytesAllocated() const { return m_DataEnd - m_Data; }

	private:
		// Disable copying
		WriteBuffer(const WriteBuffer&);
		WriteBuffer& operator= (const WriteBuffer&);

		char* m_Data;
		char* m_DataEnd;
		char* m_DataWrite;
	};


	//
	// Lightweight read buffer that uses the contents of a write buffer that must exist
	// for the life time of this read buffer.
	//
	class CLCPP_API attrReflect ReadBuffer
	{
	public:
		ReadBuffer(const WriteBuffer& write_buffer);

		// TODO: Not entirely convinced by this API with regards to the ability of its users
		//  to quickly, safely and easily detect buffer overflow scenarios before it asserts.
		void Read(void* data, unsigned int length);
		const char* ReadAt(unsigned int position) const;
		void SeekRel(int offset);

		unsigned int GetBytesRead() const { return m_DataRead - m_Data; }
		unsigned int GetTotalBytes() const { return m_DataEnd - m_Data; }
		unsigned int GetBytesRemaining() const { return m_DataEnd - m_DataRead; }

	private:
		// Disable copying
		ReadBuffer(const ReadBuffer&);
		ReadBuffer& operator= (const ReadBuffer&);

		const char* m_Data;
		const char* m_DataEnd;
		const char* m_DataRead;
	};


	// TODO: Try to merge into one function to cut down on call overhead?
	struct IPtrSave
	{
		// Normally, type is the same as field->type
		// In the case of a container, however, field points to the container and type is the value type
		virtual bool CanSavePtr(void* ptr, const clcpp::Field* field, const clcpp::Type* type) = 0;
		virtual unsigned int SavePtr(void* ptr) = 0;
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

		JSONError()
			: code(NONE)
			, position(0)
			, line(0)
			, column(0)
		{
		}

		Code code;

		// Position in the data buffer where the error occurred
		unsigned int position;

		// An attempt to specify the exact line/column where the error occurred
		// Assuming the data buffer is reasonably formatted
		unsigned int line;
		unsigned int column;
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
    CLCPP_API JSONError LoadJSON(ReadBuffer& in, void* object, const clcpp::Type* type);
    CLCPP_API JSONError LoadJSON(JSONContext& ctx, void* object, const clcpp::Field* field);

	// Save an object of a given type to the write buffer.
	// If ptr_save is null, no pointers are serialised.
	CLCPP_API void SaveJSON(WriteBuffer& out, const void* object, const clcpp::Type* type, IPtrSave* ptr_save, unsigned int flags = 0);

	// Save an object described by the given field to the write buffer.
	// If ptr_save is null, no pointers are serialised.
	CLCPP_API void SaveJSON(WriteBuffer& out, const void* object, const clcpp::Field* field, IPtrSave* ptr_save, unsigned int flags = 0);
}