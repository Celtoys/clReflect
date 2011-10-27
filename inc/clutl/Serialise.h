
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


namespace clcpp
{
	struct Type;
}


namespace clutl
{
	class DataBuffer;
	class ObjectDatabase;

	void SaveVersionedBinary(DataBuffer& out, const void* object, const clcpp::Type* type);
	void LoadVersionedBinary(DataBuffer& in, void* object, const clcpp::Type* type);


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
			OUTPUT_HEX_FLOATS = 0x20,
		};
	};


	// Cannot load nullstr fields
	JSONError LoadJSON(DataBuffer& in, void* object, const clcpp::Type* type);

	// Can save nullstr fields
	void SaveJSON(DataBuffer& out, const void* object, const clcpp::Type* type, unsigned int flags = 0);

	// Saves the entire object database
	void SaveJSON(DataBuffer& out, const ObjectDatabase& object_db, unsigned int flags = 0);
}