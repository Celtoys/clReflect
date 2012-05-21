
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clutl/JSONLexer.h>


// Standard C library function, convert string to double-precision number
// http://pubs.opengroup.org/onlinepubs/007904975/functions/strtod.html
extern "C" double strtod(const char* s00, char** se);


namespace
{
	bool isdigit(char c)
	{
		return c >= '0' && c <= '9';
	}
	bool ishexdigit(char c)
	{
		return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
	}


	int Lexer32bitHexDigits(clutl::JSONContext& ctx)
	{
		// Skip the 'u' and check for overflow
		ctx.ConsumeChar();
		if (ctx.ReadOverflows(4))
			return 0;

		// Ensure the next 4 bytes are hex digits
		// NOTE: \u is not valid - C has the equivalent \xhh and \xhhhh
		const char* digits = ctx.PeekChars();
		if (ishexdigit(digits[0]) &&
			ishexdigit(digits[1]) &&
			ishexdigit(digits[2]) &&
			ishexdigit(digits[3]))
		{
			ctx.ConsumeChars(4);
			return 4;
		}

		ctx.SetError(clutl::JSONError::EXPECTING_HEX_DIGIT);

		return 0;
	}


	int LexerEscapeSequence(clutl::JSONContext& ctx)
	{
		ctx.ConsumeChar();

		if (ctx.ReadOverflows(0))
			return 0;
		char c = ctx.PeekChar();

		switch (c)
		{
		// Pass all single character sequences
		case '\"':
		case '\\':
		case '/':
		case 'b':
		case 'n':
		case 'f':
		case 'r':
		case 't':
			ctx.ConsumeChar();
			return 1;

		// Parse the unicode hex digits
		case 'u':
			return 1 + Lexer32bitHexDigits(ctx);

		default:
			ctx.SetError(clutl::JSONError::INVALID_ESCAPE_SEQUENCE);
			return 0;
		}
	}


	clutl::JSONToken LexerString(clutl::JSONContext& ctx)
	{
		// Start off construction of the string beyond the open quote
		ctx.ConsumeChar();
		clutl::JSONToken token(clutl::JSON_TOKEN_STRING, 0);
		token.val.string = ctx.PeekChars();

		// The common case here is another character as opposed to quotes so
		// keep looping until that happens
		int len = 0;
		while (true)
		{
			if (ctx.ReadOverflows(0))
				return clutl::JSONToken();
			char c = ctx.PeekChar();

			switch (c)
			{
			// The string terminates with a quote
			case '\"':
				ctx.ConsumeChar();
				return token;

			// Escape sequence
			case '\\':
				len = LexerEscapeSequence(ctx);
				if (len == 0)
					return clutl::JSONToken();
				token.length += 1 + len;
				break;

			// A typical string character
			default:
				ctx.ConsumeChar();
				token.length++;
			}
		}

		return token;
	}


	//
	// This will return an integer in the range [-9,223,372,036,854,775,808:9,223,372,036,854,775,807]
	//
	bool LexerInteger(clutl::JSONContext& ctx, clcpp::uint64& uintval)
	{
		// Consume the first digit
		if (ctx.ReadOverflows(0))
			return false;
		char c = ctx.PeekChar();
		if (!isdigit(c))
		{
			ctx.SetError(clutl::JSONError::EXPECTING_DIGIT);
			return false;
		}

		uintval = 0;
		do 
		{
			// Consume and accumulate the digit
			ctx.ConsumeChar();
			uintval = (uintval * 10) + (c - '0');

			// Peek at the next character and leave if its not a digit
			if (ctx.ReadOverflows(0))
				return false;
			c = ctx.PeekChar();
		} while (isdigit(c));

		return true;
	}


	clutl::JSONToken LexerHexInteger(clutl::JSONContext& ctx, clutl::JSONToken& token)
	{
		clcpp::uint64& uintval = (clcpp::uint64&)token.val.integer;

		// Consume the first digit
		if (ctx.ReadOverflows(0))
			return clutl::JSONToken();
		char c = ctx.PeekChar();
		if (!ishexdigit(c))
		{
			ctx.SetError(clutl::JSONError::EXPECTING_HEX_DIGIT);
			return clutl::JSONToken();
		}

		uintval = 0;
		do
		{
			// Consume and accumulate the digit
			ctx.ConsumeChar();
			clcpp::uint64 digit = 0;
			if (c < 'A')
				digit = c - '0';
			else
				digit = (c | 0x20) - 'a' + 10;
			uintval = (uintval * 16) + digit;

			// Peek at the next character and leave if it's not a hex digit
			if (ctx.ReadOverflows(0))
				return clutl::JSONToken();
			c = ctx.PeekChar();
		} while (ishexdigit(c));

		return token;
	}


	const char* VerifyDigits(clutl::JSONContext& ctx, const char* decimal, const char* end)
	{
		while (true)
		{
			if (decimal >= end)
			{
				ctx.SetError(clutl::JSONError::UNEXPECTED_END_OF_DATA);
				return 0;
			}

			if (!isdigit(*decimal))
				break;

			decimal++;
		}

		return decimal;
	}


	bool VerifyDecimal(clutl::JSONContext& ctx, const char* decimal, int len)
	{
		// Check that there is stuff beyond the .,e,E
		if (len < 2)
		{
			ctx.SetError(clutl::JSONError::UNEXPECTED_END_OF_DATA);
			return false;
		}

		const char* end = decimal + len;
		if (*decimal++ == '.')
		{
			// Ensure there are digits trailing the decimal point
			decimal = VerifyDigits(ctx, decimal, end);
			if (decimal == 0)
				return false;

			// Only need to continue if there's an exponent
			char c = *decimal++;
			if (c != 'e' && c != 'E')
				return true;
		}

		// Skip over any pos/neg qualifiers
		char c = *decimal;
		if (c == '-' || c == '+')
			decimal++;

		// Ensure there are digits trailing the exponent
		return VerifyDigits(ctx, decimal, end) != 0;
	}


	clutl::JSONToken LexerNumber(clutl::JSONContext& ctx)
	{
		// Start off construction of an integer
		const char* number_start = ctx.PeekChars();
		clutl::JSONToken token(clutl::JSON_TOKEN_INTEGER, 0);

		// Is this a hex integer?
		clcpp::uint64 uintval;
		if (ctx.PeekChar() == '0')
		{
			if (ctx.ReadOverflows(1))
				return clutl::JSONToken();

			// Change the token type to decimal if 'd' is present, relying on the value
			// union to alias between double/int types
			switch (ctx.PeekChars()[1])
			{
			case 'd':
				token.type = clutl::JSON_TOKEN_DECIMAL;
			case 'x':
				ctx.ConsumeChars(2);
				return LexerHexInteger(ctx, token);
			}
		}

		// Consume negative
		bool is_negative = false;
		if (ctx.PeekChar() == '-')
		{
			is_negative = true;
			ctx.ConsumeChar();
		}

		// Parse integer digits
		if (!LexerInteger(ctx, uintval))
			return clutl::JSONToken();

		// Convert to signed integer
		if (is_negative)
			token.val.integer = 0ULL - uintval;
		else
			token.val.integer = uintval;

		// Is this a decimal?
		const char* decimal_start = ctx.PeekChars();
		char c = *decimal_start;
		if (c == '.' || c == 'e' || c == 'E')
		{
			if (!VerifyDecimal(ctx, decimal_start, ctx.Remaining()))
				return clutl::JSONToken();

			// Re-evaluate as a decimal using the more expensive strtod function
			char* number_end;
			token.type = clutl::JSON_TOKEN_DECIMAL;
			token.val.decimal = strtod(number_start, &number_end);

			// Skip over the parsed decimal
			ctx.ConsumeChars(number_end - decimal_start);
		}

		return token;
	}


	clutl::JSONToken LexerKeyword(clutl::JSONContext& ctx, clutl::JSONTokenType type, const char* keyword, int len)
	{
		// Consume the matched first letter
		ctx.ConsumeChar();

		// Try to match the remaining letters of the keyword
		int dlen = len;
		while (dlen)
		{
			if (ctx.ReadOverflows(0))
				return clutl::JSONToken();

			// Early out when keyword no longer matches
			if (*keyword++ != ctx.PeekChar())
				break;

			ctx.ConsumeChar();
			dlen--;
		}

		if (dlen)
		{
			ctx.SetError(clutl::JSONError::INVALID_KEYWORD);
			return clutl::JSONToken();
		}

		return clutl::JSONToken(type, len + 1);
	}
}


clutl::JSONContext::JSONContext(clutl::ReadBuffer& read_buffer)
	: m_ReadBuffer(read_buffer)
	, m_Line(1)
	, m_LinePosition(0)
	, m_StackPosition(0xFFFFFFFF)
{
}

unsigned int clutl::JSONContext::ConsumeChars(int size)
{
	unsigned int pos = m_ReadBuffer.GetBytesRead();
	m_ReadBuffer.SeekRel(size);
	return pos;
}


unsigned int clutl::JSONContext::ConsumeChar()
{
	return ConsumeChars(1);
}

// Take a peek at the next N characters in the data buffer
const char* clutl::JSONContext::PeekChars()
{
	return m_ReadBuffer.ReadAt(m_ReadBuffer.GetBytesRead());
}


char clutl::JSONContext::PeekChar()
{
	return *PeekChars();
}


bool clutl::JSONContext::ReadOverflows(int size, clutl::JSONError::Code code)
{
	if (m_ReadBuffer.GetBytesRead() + size >= m_ReadBuffer.GetTotalBytes())
	{
		SetError(code);
		return true;
	}
	return false;
}


unsigned int clutl::JSONContext::Remaining() const
{
	return m_ReadBuffer.GetBytesRemaining();
}


void clutl::JSONContext::SetError(clutl::JSONError::Code code)
{
	if (m_Error.code == clutl::JSONError::NONE)
	{
		m_Error.code = code;
		m_Error.position = m_ReadBuffer.GetBytesRead();
		m_Error.line = m_Line;
		m_Error.column = m_Error.position - m_LinePosition;
	}
}


void clutl::JSONContext::IncLine()
{
	m_Line++;
	m_LinePosition = m_ReadBuffer.GetBytesRead();
}


void clutl::JSONContext::PushState(const clutl::JSONToken& token)
{
	clcpp::internal::Assert(m_StackPosition == 0xFFFFFFFF);

	// Push
	m_StackPosition = m_ReadBuffer.GetBytesRead();
	m_StackToken = token;
}


void clutl::JSONContext::PopState(clutl::JSONToken& token)
{
	clcpp::internal::Assert(m_StackPosition != 0xFFFFFFFF);

	// Restore state
	int offset = m_ReadBuffer.GetBytesRead() - m_StackPosition;
	m_ReadBuffer.SeekRel(-offset);
	token = m_StackToken;

	// Pop
	m_StackPosition = 0xFFFFFFFF;
	m_StackToken = clutl::JSONToken();
}


clutl::JSONToken clutl::LexerNextToken(clutl::JSONContext& ctx)
{
start:
	// Read the current character and return an empty token at stream end
	if (ctx.ReadOverflows(0, clutl::JSONError::NONE))
		return clutl::JSONToken();
	char c = ctx.PeekChar();

	switch (c)
	{
	// Branch to the start only if it's a whitespace (the least-common case)
	case '\n':
		ctx.IncLine();
	case ' ':
	case '\t':
	case '\v':
	case '\f':
	case '\r':
		ctx.ConsumeChar();
		goto start;

	// Structural single character tokens
	case '{':
	case '}':
	case ',':
	case '[':
	case ']':
	case ':':
		ctx.ConsumeChar();
		return clutl::JSONToken((clutl::JSONTokenType)c, 1);

	// Strings
	case '\"':
		return LexerString(ctx);

	// Integer or floating point numbers
	case '-':
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		return LexerNumber(ctx);

	// Keywords
	case 't': return LexerKeyword(ctx, clutl::JSON_TOKEN_TRUE, "rue", 3);
	case 'f': return LexerKeyword(ctx, clutl::JSON_TOKEN_FALSE, "alse", 4);
	case 'n': return LexerKeyword(ctx, clutl::JSON_TOKEN_NULL, "ull", 3);

	default:
		ctx.SetError(clutl::JSONError::UNEXPECTED_CHARACTER);
		return clutl::JSONToken();
	}
}
