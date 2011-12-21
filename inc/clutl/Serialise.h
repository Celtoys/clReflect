
//
// ===============================================================================
// clReflect, Serialise.h - All different types of serialisation that clReflect
// supports.
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

#pragma once


#include <clcpp/clcpp.h>


// Partially reflected so that it can be used for reflecting custom serialisation functions
clcpp_reflect_part(clutl::JSONToken)


namespace clutl
{
	struct Object;


	//
	// Growable write byte buffer
	//
	class WriteBuffer
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

		void SeekRel(int offset);

		const char* GetData() const { return m_Data; }
		unsigned int GetBytesWritten() const { return m_DataWrite - m_Data; }

	private:
		char* m_Data;
		char* m_DataEnd;
		char* m_DataWrite;
	};


	//
	// Lightweight read buffer that uses the contents of a write buffer that must exist
	// for the life time of this read buffer.
	//
	class ReadBuffer
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
		const char* m_Data;
		const char* m_DataEnd;
		const char* m_DataRead;
	};


	// Binary serialisation
	void SaveVersionedBinary(WriteBuffer& out, const void* object, const clcpp::Type* type);
	void LoadVersionedBinary(ReadBuffer& in, void* object, const clcpp::Type* type);


	struct JSONError
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
			EMIT_HEX_FLOATS = 0x20
		};
	};


	enum JSONTokenType
	{
		JSON_TOKEN_NONE,

		// Single character tokens match their character values for simpler switch code
		JSON_TOKEN_LBRACE = '{',
		JSON_TOKEN_RBRACE = '}',
		JSON_TOKEN_COMMA = ',',
		JSON_TOKEN_COLON = ':',
		JSON_TOKEN_LBRACKET = '[',
		JSON_TOKEN_RBRACKET = ']',

		JSON_TOKEN_STRING,

		JSON_TOKEN_TRUE,
		JSON_TOKEN_FALSE,
		JSON_TOKEN_NULL,

		JSON_TOKEN_INTEGER,
		JSON_TOKEN_DECIMAL,
	};


	struct JSONToken
	{
		JSONToken()
			: type(JSON_TOKEN_NONE)
			, length(0)
		{
		}

		explicit JSONToken(JSONTokenType type, int length)
			: type(type)
			, length(length)
		{
		}

		bool IsValid() const
		{
			return type != JSON_TOKEN_NONE;
		}

		JSONTokenType type;
		int length;

		// All possible token value representations
		struct
		{
			union
			{
				const char* string;
				__int64 integer;
				double decimal;
			};
		} val;
	};


	// JSON serialisation
	JSONError LoadJSON(ReadBuffer& in, void* object, const clcpp::Type* type);
	void SaveJSON(WriteBuffer& out, const void* object, const clcpp::Type* type, unsigned int flags = 0);
}