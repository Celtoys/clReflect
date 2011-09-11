
//
// IEEE754 text encode/decode of double values
//

#include <clutl/Serialise.h>
#include <clutl/Containers.h>


extern "C" double strtod(const char* s00, char** se);


namespace
{
	enum TokenType
	{
		TT_NONE,

		// Single character tokens match their character values for simpler switch code
		TT_LBRACE = '{',
		TT_RBRACE = '}',
		TT_COMMA = ',',
		TT_COLON = ':',
		TT_LBRACKET = '[',
		TT_RBRACKET = ']',

		TT_STRING,

		TT_TRUE,
		TT_FALSE,
		TT_NULL,

		TT_INTEGER,
		TT_DECIMAL,
	};


	struct Token
	{
		Token()
			: type(TT_NONE)
			, position(0)
			, length(0)
		{
		}

		explicit Token(TokenType type, int position, int length)
			: type(type)
			, position(position)
			, length(length)
		{
		}

		TokenType type;
		int position;
		int length;

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


	// TODO: buffer overflow checking without the data buffer asserting!


	bool isdigit(char c)
	{
		return c >= '0' && c <= '9';
	}
	bool ishexdigit(char c)
	{
		return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
	}


	bool Lexer32bitHexDigits(clutl::DataBuffer& in)
	{
		in.SeekRel(1);

		// TODO: verify there are enough bytes to read 4 characters
		// NOTE: \u is not valid - C has the equivalent \xhh and \xhhhh

		// Ensure the next 4 bytes are hex digits
		int pos = in.GetPosition();
		const char* digits = in.ReadAt(pos);
		return
			ishexdigit(digits[0]) &&
			ishexdigit(digits[1]) &&
			ishexdigit(digits[2]) &&
			ishexdigit(digits[3]);
	}


	bool LexerEscapeSequence(clutl::DataBuffer& in)
	{
		in.SeekRel(1);

		int pos = in.GetPosition();
		char c = *in.ReadAt(pos);

		switch (c)
		{
		// Pass all single character sequences
		case ('\"'):
		case ('\\'):
		case ('/'):
		case ('\b'):
		case ('\f'):
		case ('\n'):
		case ('\r'):
		case ('\t'):
			in.SeekRel(1);
			return true;

		// Parse the unicode hex digits
		case ('u'):
			return Lexer32bitHexDigits(in);

		default:
			// ERROR: Invalid escape sequence
			return false;
		}
	}


	Token LexerString(clutl::DataBuffer& in)
	{
		// Start off construction of the string
		int pos = in.GetPosition();
		Token token(TT_STRING, pos, 0);
		in.SeekRel(1);
		token.val.string = in.ReadAt(pos);

		// The common case here is another character as opposed to quotes so
		// keep looping until that happens
		while (true)
		{
			pos = in.GetPosition();
			char c = *in.ReadAt(pos);

			switch (c)
			{
			// The string terminates with a quote
			case ('\"'):
				in.SeekRel(1);
				return token;

			// Escape sequence
			case ('\\'):
				if (!LexerEscapeSequence(in))
					return Token();
				token.length += in.GetPosition() - pos;
				break;

			// A typical string character
			default:
				in.SeekRel(1);
				token.length++;
			}
		}

		return token;
	}


	//
	// This will return an integer in the range [-9,223,372,036,854,775,808:9,223,372,036,854,775,807]
	//
	bool LexerInteger(clutl::DataBuffer& in, unsigned __int64& uintval)
	{
		// Consume the first digit
		int pos = in.GetPosition();
		char c = *in.ReadAt(pos);
		if (!isdigit(c))
			return false;

		uintval = 0;
		do 
		{
			// Consume and accumulate the digit
			in.SeekRel(1);
			uintval = (uintval * 10) + (c - '0');

			// Peek at the next character and leave if its not a digit
			pos = in.GetPosition();
			c = *in.ReadAt(pos);
		} while (isdigit(c));

		return true;
	}


	Token LexerNumber(clutl::DataBuffer& in)
	{
		// Start off construction of an integer
		int start_pos = in.GetPosition();
		Token token(TT_INTEGER, start_pos, 0);

		// Consume negative
		bool is_negative = false;
		if (*in.ReadAt(start_pos) == '-')
		{
			is_negative = true;
			in.SeekRel(1);
		}

		// Parse the integer digits
		unsigned __int64 uintval;
		if (!LexerInteger(in, uintval))
			// TODO: error
			return Token();

		// Convert to signed integer
		if (is_negative)
			token.val.integer = 0ULL - uintval;
		else
			token.val.integer = uintval;

		// Is this a decimal?
		int pos = in.GetPosition();
		char c = *in.ReadAt(pos);
		if (c == '.')
		{
			// Re-evaluate as a decimal using the more expensive strtod function
			const char* decimal_start = in.ReadAt(start_pos);
			char* decimal_end;
			token.type = TT_DECIMAL;
			token.val.decimal = strtod(decimal_start, &decimal_end);

			// Skip over the parsed decimal
			in.SeekAbs(start_pos + (decimal_end - decimal_start));
		}

		return token;
	}


	Token LexerKeyword(clutl::DataBuffer& in, TokenType type, const char* keyword, int len)
	{
		// Consume the first letter
		int start_pos = in.GetPosition();
		in.SeekRel(1);

		// TODO: Overflow without buffer assert

		// Try to match the remaining letters of the keyword
		int pos = in.GetPosition();
		while (len && *in.ReadAt(pos) == *keyword)
		{
			keyword++;
			pos++;
		}

		if (len)
			// ERROR: Keyword didn't match
			return Token();

		return Token(type, start_pos, in.GetPosition() - start_pos);
	}


	Token LexerToken(clutl::DataBuffer& in)
	{
	start:
		unsigned int pos = in.GetPosition();
		char c = *in.ReadAt(pos);

		switch (c)
		{
		// Branch to the start only if it's a whitespace (the least-common case)
		case (' '):
		case ('\t'):
		case ('\n'):
		case ('\v'):
		case ('\f'):
		case ('\r'):
			in.SeekRel(1);
			goto start;

		// Structural single character tokens
		case ('{'):
		case ('}'):
		case (','):
		case ('['):
		case (']'):
			in.SeekRel(1);
			return Token((TokenType)c, pos, 1);

		// Strings
		case ('\"'):
			return LexerString(in);

		// Integer or floating point numbers
		case ('-'):
		case ('0'):
		case ('1'):
		case ('2'):
		case ('3'):
		case ('4'):
		case ('5'):
		case ('6'):
		case ('7'):
		case ('8'):
		case ('9'):
			return LexerNumber(in);

		// Keywords
		case ('t'): return LexerKeyword(in, TT_TRUE, "rue", 3);
		case ('f'): return LexerKeyword(in, TT_FALSE, "alse", 4);
		case ('n'): return LexerKeyword(in, TT_NULL, "ull", 3);

		default:
			// ERROR: Unexpected character
			break;
		}

		return Token();
	}


	void ParserValue(clutl::DataBuffer& in, Token& t);
	void ParserObject(clutl::DataBuffer& in, Token& t);


	Token Expect(clutl::DataBuffer& in, Token& t, TokenType type)
	{
		if (t.type != type)
			// ERROR
			return Token();
		Token old = t;
		t = LexerToken(in);
		return old;
	}


	void ParserString(const Token& t)
	{
	}


	void ParserInteger(const Token& t)
	{
	}


	void ParserDecimal(const Token& t)
	{
	}


	void ParserElements(clutl::DataBuffer& in, Token& t)
	{
		// Expect a value first
		ParserValue(in, t);

		if (t.type == TT_COMMA)
		{
			t = LexerToken(in);
			ParserElements(in, t);
		}
	}


	void ParserArray(clutl::DataBuffer& in, Token& t)
	{
		Expect(in, t, TT_LBRACKET);

		if (t.type == TT_RBRACKET)
		{
			t = LexerToken(in);
			return;
		}

		ParserElements(in, t);
		Expect(in, t, TT_RBRACKET);
	}


	void ParserLiteralValue(clutl::DataBuffer& in, Token& t, TokenType type, int val)
	{
		if (t.type != type)
			// ERROR
			return;
		// process integer
		t = LexerToken(in);
	}


	void ParserValue(clutl::DataBuffer& in, Token& t)
	{
		switch (t.type)
		{
		case (TT_STRING): return ParserString(Expect(in, t, TT_STRING));
		case (TT_INTEGER): return ParserInteger(Expect(in, t, TT_INTEGER));
		case (TT_DECIMAL): return ParserDecimal(Expect(in, t, TT_DECIMAL));
		case (TT_LBRACE): return ParserObject(in, t);
		case (TT_LBRACKET): return ParserArray(in, t);
		case (TT_TRUE): return ParserLiteralValue(in, t, TT_TRUE, 1);
		case (TT_FALSE): return ParserLiteralValue(in, t, TT_FALSE, 0);
		case (TT_NULL): return ParserLiteralValue(in, t, TT_NULL, 0);

		default:
			// ERROR unexpected token
			break;
		}
	}


	void ParserPair(clutl::DataBuffer& in, Token& t)
	{
		Expect(in, t, TT_STRING);
		Expect(in, t, TT_COLON);
		ParserValue(in, t);
	}


	void ParserMembers(clutl::DataBuffer& in, Token& t)
	{
		ParserPair(in, t);

		if (t.type == TT_COMMA)
		{
			t = LexerToken(in);
			ParserMembers(in, t);
		}
	}


	void ParserObject(clutl::DataBuffer& in, Token& t)
	{
		Expect(in, t, TT_LBRACE);

		if (t.type == TT_RBRACE)
		{
			t = LexerToken(in);
			return;
		}

		ParserMembers(in, t);
		Expect(in, t, TT_RBRACE);
	}
}