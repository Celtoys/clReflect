
//
// ===============================================================================
// clReflect, JSONLexer.h - A fast, self-contained JSON lexer
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once


#include <clcpp/clcpp.h>
#include <clutl/Serialise.h>


clcpp_reflect_part(clutl)
namespace clutl
{
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


	// Partially reflected so that it can be used for reflecting custom serialisation functions
	struct CLCPP_API clcpp_attr(reflect_part) JSONToken
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
				clcpp::int64 integer;
				double decimal;
			};
		} val;
	};



	//
	// The main lexer/parser context, for keeping tracking of errors and providing a level of
	// text parsing abstraction above the data buffer.
	//
	class CLCPP_API JSONContext
	{
	public:
		JSONContext(clutl::ReadBuffer& read_buffer);

		// Consume the given amount of characters in the data buffer, assuming
		// they have been parsed correctly. The original position before the
		// consume operation is returned.
		unsigned int ConsumeChars(int size);
		unsigned int ConsumeChar();

		// Take a peek at the next N characters in the data buffer
		const char* PeekChars();
		char PeekChar();

		// Test to see if reading a specific count of characters would overflow the input
		// data buffer. Automatically sets the error code as a result.
		bool ReadOverflows(int size, clutl::JSONError::Code code = clutl::JSONError::UNEXPECTED_END_OF_DATA);

		// How many bytes are left to parse?
		unsigned int Remaining() const;

		// Record the first error only, along with its position
		void SetError(clutl::JSONError::Code code);

		// Increment the current line for error reporting
		void IncLine();

		void PushState(const clutl::JSONToken& token);
		void PopState(clutl::JSONToken& token);

		clutl::JSONError GetError() const { return m_Error; }


	private:
		// Parsing state
		clutl::ReadBuffer& m_ReadBuffer;
		clutl::JSONError m_Error;
		unsigned int m_Line;
		unsigned int m_LinePosition;

		// One-level deep parsing state stack
		unsigned int m_StackPosition;
		clutl::JSONToken m_StackToken;
	};


	CLCPP_API clutl::JSONToken LexerNextToken(clutl::JSONContext& ctx);
}
