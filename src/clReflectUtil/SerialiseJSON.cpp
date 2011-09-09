
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


	bool Parse32bitHexDigits(clutl::DataBuffer& in)
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


	bool ParseEscapeSequence(clutl::DataBuffer& in)
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
			return Parse32bitHexDigits(in);

		default:
			// ERROR: Invalid escape sequence
			return false;
		}
	}


	Token ParseString(clutl::DataBuffer& in)
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
				if (!ParseEscapeSequence(in))
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
	bool ParseInteger(clutl::DataBuffer& in, unsigned __int64& uintval)
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


	Token ParseNumber(clutl::DataBuffer& in)
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
		if (!ParseInteger(in, uintval))
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


	Token ParseToken(clutl::DataBuffer& in)
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
			return ParseString(in);

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
			return ParseNumber(in);

		default:
			// sequence of chars
			break;
		}

		return Token();
	}
}