
//
// TODO:
//    * Allow names to be specified as CRCs?
//    * Allow IEEE754 hex float representation.
//    * Escape sequences need converting.
//    * Enums communicated by value.
//    * Field names need to be in memory for JSON serialising to work.
//

#include <clutl/Serialise.h>
#include <clutl/Containers.h>
#include <clcpp/Database.h>


// Explicitly stated dependencies in stdlib.h
extern "C" double strtod(const char* s00, char** se);
// This is Microsoft's safer version of the ISO C++ _fcvt which takes the extra buffer and size parameters
// for overflow checking and thread safety
extern "C" int _fcvt_s(char* buffer, size_t buffer_size, double val, int count, int* dec, int* sign);

// TODO: REMOVE
extern "C" int printf(const char* format, ...);


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
		// An empty token to prevent the need to construct tokens for comparison
		static Token Null;

		Token()
			: type(TT_NONE)
			, length(0)
		{
		}

		explicit Token(TokenType type, int length)
			: type(type)
			, length(length)
		{
		}

		bool IsValid() const
		{
			return type != TT_NONE;
		}

		TokenType type;
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

	// Definition of the null token
	Token Token::Null;


	bool isdigit(char c)
	{
		return c >= '0' && c <= '9';
	}
	bool ishexdigit(char c)
	{
		return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
	}


	//
	// The main lexer/parser context, for keeping tracking of errors and providing a level of
	// text parsing abstraction above the data buffer.
	//
	class Context
	{
	public:
		Context(clutl::DataBuffer& data_buffer)
			: m_DataBuffer(data_buffer)
			, m_Line(1)
			, m_LinePosition(0)
		{
		}

		// Consume the given amount of characters in the data buffer, assuming
		// they have been parsed correctly. The original position before the
		// consume operation is returned.
		unsigned int ConsumeChars(int size)
		{
			unsigned int pos = m_DataBuffer.GetPosition();
			m_DataBuffer.SeekRel(size);
			return pos;
		}
		unsigned int ConsumeChar()
		{
			return ConsumeChars(1);
		}

		// Take a peek at the next N characters in the data buffer
		const char* PeekChars()
		{
			return m_DataBuffer.ReadAt(m_DataBuffer.GetPosition());
		}
		char PeekChar()
		{
			return *PeekChars();
		}

		// Test to see if reading a specific count of characters would overflow the input
		// data buffer. Automatically sets the error code as a result.
		bool ReadOverflows(int size, clutl::JSONError::Code code = clutl::JSONError::UNEXPECTED_END_OF_DATA)
		{
			if (m_DataBuffer.GetPosition() + size >= m_DataBuffer.GetSize())
			{
				SetError(code);
				return true;
			}
			return false;
		}

		// How many bytes are left to parse?
		unsigned int Remaining() const
		{
			return m_DataBuffer.GetSize() - m_DataBuffer.GetPosition();
		}

		// Record the first error only, along with its position
		void SetError(clutl::JSONError::Code code)
		{
			if (m_Error.code == clutl::JSONError::NONE)
			{
				m_Error.code = code;
				m_Error.position = m_DataBuffer.GetPosition();
				m_Error.line = m_Line;
				m_Error.column = m_Error.position - m_LinePosition;
			}
		}

		// Increment the current line for error reporting
		void IncLine()
		{
			m_Line++;
			m_LinePosition = m_DataBuffer.GetPosition();
		}

		clutl::JSONError GetError() const
		{
			return m_Error;
		}

	private:
		clutl::DataBuffer& m_DataBuffer;
		clutl::JSONError m_Error;
		unsigned int m_Line;
		unsigned int m_LinePosition;
	};


	int Lexer32bitHexDigits(Context& ctx)
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


	int LexerEscapeSequence(Context& ctx)
	{
		ctx.ConsumeChar();

		if (ctx.ReadOverflows(0))
			return 0;
		char c = ctx.PeekChar();

		switch (c)
		{
		// Pass all single character sequences
		case ('\"'):
		case ('\\'):
		case ('/'):
		case ('b'):
		case ('n'):
		case ('f'):
		case ('r'):
		case ('t'):
			ctx.ConsumeChar();
			return 1;

		// Parse the unicode hex digits
		case ('u'):
			return 1 + Lexer32bitHexDigits(ctx);

		default:
			ctx.SetError(clutl::JSONError::INVALID_ESCAPE_SEQUENCE);
			return 0;
		}
	}


	Token LexerString(Context& ctx)
	{
		// Start off construction of the string beyond the open quote
		ctx.ConsumeChar();
		Token token(TT_STRING, 0);
		token.val.string = ctx.PeekChars();

		// The common case here is another character as opposed to quotes so
		// keep looping until that happens
		int len = 0;
		while (true)
		{
			if (ctx.ReadOverflows(0))
				return Token::Null;
			char c = ctx.PeekChar();

			switch (c)
			{
			// The string terminates with a quote
			case ('\"'):
				ctx.ConsumeChar();
				return token;

			// Escape sequence
			case ('\\'):
				len = LexerEscapeSequence(ctx);
				if (len == 0)
					return Token::Null;
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
	bool LexerInteger(Context& ctx, unsigned __int64& uintval)
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


	const char* VerifyDigits(Context& ctx, const char* decimal, const char* end)
	{
		do
		{
			if (decimal >= end)
			{
				ctx.SetError(clutl::JSONError::UNEXPECTED_END_OF_DATA);
				return 0;
			}
		} while (isdigit(*decimal++));

		return decimal;
	}


	bool VerifyDecimal(Context& ctx, const char* decimal, int len)
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


	Token LexerNumber(Context& ctx)
	{
		// Start off construction of an integer
		const char* number_start = ctx.PeekChars();
		Token token(TT_INTEGER, 0);

		// Consume negative
		bool is_negative = false;
		if (ctx.PeekChar() == '-')
		{
			is_negative = true;
			ctx.ConsumeChar();
		}

		// Parse the integer digits
		unsigned __int64 uintval;
		if (!LexerInteger(ctx, uintval))
			return Token::Null;

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
				return Token::Null;

			// Re-evaluate as a decimal using the more expensive strtod function
			char* number_end;
			token.type = TT_DECIMAL;
			token.val.decimal = strtod(number_start, &number_end);

			// Skip over the parsed decimal
			ctx.ConsumeChars(number_end - decimal_start);
		}

		return token;
	}


	Token LexerKeyword(Context& ctx, TokenType type, const char* keyword, int len)
	{
		// Consume the matched first letter
		ctx.ConsumeChar();

		// Try to match the remaining letters of the keyword
		int dlen = len;
		while (dlen)
		{
			if (ctx.ReadOverflows(0))
				return Token::Null;

			// Early out when keyword no longer matches
			if (*keyword++ != ctx.PeekChar())
				break;

			ctx.ConsumeChar();
			dlen--;
		}

		if (dlen)
		{
			ctx.SetError(clutl::JSONError::INVALID_KEYWORD);
			return Token::Null;
		}

		return Token(type, len + 1);
	}


	Token LexerToken(Context& ctx)
	{
	start:
		// Read the current character and return an empty token at stream end
		if (ctx.ReadOverflows(0, clutl::JSONError::NONE))
			return Token::Null;
		char c = ctx.PeekChar();

		switch (c)
		{
		// Branch to the start only if it's a whitespace (the least-common case)
		case ('\n'):
			ctx.IncLine();
		case (' '):
		case ('\t'):
		case ('\v'):
		case ('\f'):
		case ('\r'):
			ctx.ConsumeChar();
			goto start;

		// Structural single character tokens
		case ('{'):
		case ('}'):
		case (','):
		case ('['):
		case (']'):
		case (':'):
			ctx.ConsumeChar();
			return Token((TokenType)c, 1);

		// Strings
		case ('\"'):
			return LexerString(ctx);

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
			return LexerNumber(ctx);

		// Keywords
		case ('t'): return LexerKeyword(ctx, TT_TRUE, "rue", 3);
		case ('f'): return LexerKeyword(ctx, TT_FALSE, "alse", 4);
		case ('n'): return LexerKeyword(ctx, TT_NULL, "ull", 3);

		default:
			ctx.SetError(clutl::JSONError::UNEXPECTED_CHARACTER);
			return Token::Null;
		}
	}


	void ParserValue(Context& ctx, Token& t);
	void ParserObject(Context& ctx, Token& t);


	Token Expect(Context& ctx, Token& t, TokenType type)
	{
		// Check the tokens match
		if (t.type != type)
		{
			ctx.SetError(clutl::JSONError::UNEXPECTED_TOKEN);
			return Token::Null;
		}

		// Look-ahead one token
		Token old = t;
		t = LexerToken(ctx);
		return old;
	}


	void ParserString(const Token& t)
	{
		// Was there an error expecting a string?
		if (!t.IsValid())
			return;

		printf("%.*s ", t.length, t.val.string);
	}


	void ParserInteger(const Token& t)
	{
		// Was there an error expecting an integer
		if (!t.IsValid())
			return;

		printf("%d ", t.val.integer);
	}


	void ParserDecimal(const Token& t)
	{
		// Was there an error expecting a decimal?
		if (!t.IsValid())
			return;

		printf("%f ", t.val.decimal);
	}


	void ParserElements(Context& ctx, Token& t)
	{
		// Expect a value first
		ParserValue(ctx, t);

		if (t.type == TT_COMMA)
		{
			printf(", ");
			t = LexerToken(ctx);
			ParserElements(ctx, t);
		}
	}


	void ParserArray(Context& ctx, Token& t)
	{
		if (!Expect(ctx, t, TT_LBRACKET).IsValid())
			return;
		printf("[ ");

		// Empty array?
		if (t.type == TT_RBRACKET)
		{
			printf("] ");
			t = LexerToken(ctx);
			return;
		}

		ParserElements(ctx, t);
		Expect(ctx, t, TT_RBRACKET);
		printf("] ");
	}


	void ParserLiteralValue(Token& t, int val)
	{
		// Was there an error expecting a literal value?
		if (!t.IsValid())
			return;

		printf("%d ", val);
	}


	void ParserValue(Context& ctx, Token& t)
	{
		switch (t.type)
		{
		case (TT_STRING): return ParserString(Expect(ctx, t, TT_STRING));
		case (TT_INTEGER): return ParserInteger(Expect(ctx, t, TT_INTEGER));
		case (TT_DECIMAL): return ParserDecimal(Expect(ctx, t, TT_DECIMAL));
		case (TT_LBRACE): return ParserObject(ctx, t);
		case (TT_LBRACKET): return ParserArray(ctx, t);
		case (TT_TRUE): return ParserLiteralValue(Expect(ctx, t, TT_TRUE), 1);
		case (TT_FALSE): return ParserLiteralValue(Expect(ctx, t, TT_FALSE), 0);
		case (TT_NULL): return ParserLiteralValue(Expect(ctx, t, TT_NULL), 0);

		default:
			ctx.SetError(clutl::JSONError::UNEXPECTED_TOKEN);
			break;
		}
	}


	void ParserPair(Context& ctx, Token& t)
	{
		Token o = Expect(ctx, t, TT_STRING);
		if (!o.IsValid())
			return;
		printf("%.*s ", o.length, o.val.string);

		if (!Expect(ctx, t, TT_COLON).IsValid())
			return;
		printf(": ");

		ParserValue(ctx, t);
	}


	void ParserMembers(Context& ctx, Token& t)
	{
		ParserPair(ctx, t);

		if (t.type == TT_COMMA)
		{
			printf(", ");
			t = LexerToken(ctx);
			ParserMembers(ctx, t);
		}
	}


	void ParserObject(Context& ctx, Token& t)
	{
		if (!Expect(ctx, t, TT_LBRACE).IsValid())
			return;
		printf("{ ");

		// Empty object?
		if (t.type == TT_RBRACE)
		{
			t = LexerToken(ctx);
			printf("} ");
			return;
		}

		ParserMembers(ctx, t);
		Expect(ctx, t, TT_RBRACE);
		printf("} ");
	}
}


clutl::JSONError clutl::LoadJSON(DataBuffer& in, void* object, const clcpp::Type* type)
{
	Context ctx(in);
	Token t = LexerToken(ctx);
	ParserObject(ctx, t);
	return ctx.GetError();
}


namespace
{
	// A save function lookup table for all supported C++ types
	typedef void (*SaveIntegerFunc)(clutl::DataBuffer&, const char*);
	static const int g_HashSaveIntegerMod = 47;
	SaveIntegerFunc g_SaveIntegerLookupTable[g_HashSaveIntegerMod] = { 0 };
	bool g_SaveIntegerLookupTableReady = false;


	// For the given data set of basic type name hashes, this combines to make
	// a perfect hash function with no collisions, allowing quick indexed lookup.
	unsigned int GetSaveIntegerFuncIndex(unsigned int hash)
	{
		return hash % g_HashSaveIntegerMod;
	}
	unsigned int GetSaveIntegerFuncIndex(const char* type_name)
	{
		return GetSaveIntegerFuncIndex(clcpp::internal::HashNameString(type_name));
	}


	void AddSaveIntegerFunc(const char* type_name, SaveIntegerFunc func)
	{
		// Ensure there are no collisions before adding the function
		unsigned index = GetSaveIntegerFuncIndex(type_name);
		clcpp::internal::Assert(g_SaveIntegerLookupTable[index] == 0 && "Lookup table index collision");
		g_SaveIntegerLookupTable[index] = func;
	}


	void SaveObject(clutl::DataBuffer& out, const char* object, const clcpp::Type* type);


	void SaveString(clutl::DataBuffer& out, const char* str)
	{
		out.Write("\"", 1);
		const char* start = str;
		while (*str++)
			;
		out.Write(start, str - start - 1);
		out.Write("\"", 1);
	}


	void SaveInteger(clutl::DataBuffer& out, __int64 integer)
	{
		// Enough to store a 64-bit int
		static const int MAX_SZ = 20;
		char text[MAX_SZ];

		// Null terminate and start at the end
		text[MAX_SZ - 1] = 0;
		char* end = text + MAX_SZ - 1;
		char* tptr = end;

		// Get the absolute and record if the value is negative
		bool negative = false;
		if (integer < 0)
		{
			negative = true;
			integer = -integer;
		}

		// Loop through the value with radix 10
		do 
		{
			__int64 next_integer = integer / 10;
			*--tptr = char('0' + (integer - next_integer * 10));
			integer = next_integer;
		} while (integer);

		// Add negative prefix
		if (negative)
			*--tptr = '-';

		out.Write(tptr, end - tptr);
	}


	void SaveUnsignedInteger(clutl::DataBuffer& out, unsigned __int64 integer)
	{
		// Enough to store a 64-bit int + null
		static const int MAX_SZ = 21;
		char text[MAX_SZ];

		// Null terminate and start at the end
		text[MAX_SZ - 1] = 0;
		char* end = text + MAX_SZ - 1;
		char* tptr = end;

		// Loop through the value with radix 10
		do 
		{
			__int64 next_integer = integer / 10;
			*--tptr = char('0' + (integer - next_integer * 10));
			integer = next_integer;
		} while (integer);

		out.Write(tptr, end - tptr);
	}


	// Automate the process of de-referencing an object as its exact type so no
	// extra bytes are read incorrectly and values are correctly zero-extended.
	template <typename TYPE>
	void SaveIntegerWithCast(clutl::DataBuffer& out, const char* object)
	{
		SaveInteger(out, *(TYPE*)object);
	}
	template <typename TYPE>
	void SaveUnsignedIntegerWithCast(clutl::DataBuffer& out, const char* object)
	{
		SaveUnsignedInteger(out, *(TYPE*)object);
	}


	void SaveDecimal(clutl::DataBuffer& out, double decimal)
	{
		// Convert the double to a string on the local stack
		char fcvt_buffer[512] = { 0 };
		int dec = -1, sign = -1;
		_fcvt_s(fcvt_buffer, sizeof(fcvt_buffer), decimal, 40, &dec, &sign);

		char decimal_buffer[512];
		char* iptr = fcvt_buffer;
		char* optr = decimal_buffer;

		// Prefix with negative sign
		if (sign)
			*optr++ = '-';

		/**optr++ = *iptr++;
		*optr++ = '.';

		char* last_nonzero_digit = optr;
		while (*iptr)
		{
			*optr++ = *iptr++;

			// Keep track of the last non-zero digit
			if (*iptr && *iptr != '0')
				last_nonzero_digit = optr;
		}
		optr = last_nonzero_digit;
		*optr++ = 'e';
		*optr = 0;

		out.Write(decimal_buffer, optr - decimal_buffer);

		SaveInteger(out, dec - 1);*/

		// With a negative decimal position, prefix with 0. and however many
		// zeroes are needed
		if (dec <= 0)
		{
			*optr++ = '0';
			*optr++ = '.';

			dec = -dec;
			while (--dec > 0)
				*optr++ = '0';

			dec = -1;
		}

		// Copy between buffers
		char* last_nonzero_digit = optr;
		while (*iptr)
		{
			// Insert decimal point
			if (iptr - fcvt_buffer == dec)
			{
				*optr++ = '.';
				last_nonzero_digit = optr;
			}

			*optr++ = *iptr++;

			// Keep track of the last non-zero digit
			if (*iptr && *iptr != '0')
				last_nonzero_digit = optr;
		}

		*(last_nonzero_digit + 1) = 0;
		out.Write(decimal_buffer, last_nonzero_digit - decimal_buffer + 1);
	}


	void SaveDouble(clutl::DataBuffer& out, const char* object)
	{
		SaveDecimal(out, *(double*)object);
	}
	void SaveFloat(clutl::DataBuffer& out, const char* object)
	{
		SaveDecimal(out, *(float*)object);
	}


	void SetupSaveIntegerLookupTable()
	{
		if (!g_SaveIntegerLookupTableReady)
		{
			// Add all integers
			AddSaveIntegerFunc("bool", SaveIntegerWithCast<bool>);
			AddSaveIntegerFunc("char", SaveIntegerWithCast<char>);
			AddSaveIntegerFunc("wchar_t", SaveUnsignedIntegerWithCast<wchar_t>);
			AddSaveIntegerFunc("unsigned char", SaveUnsignedIntegerWithCast<unsigned char>);
			AddSaveIntegerFunc("short", SaveIntegerWithCast<short>);
			AddSaveIntegerFunc("unsigned short", SaveUnsignedIntegerWithCast<unsigned short>);
			AddSaveIntegerFunc("int", SaveIntegerWithCast<int>);
			AddSaveIntegerFunc("unsigned int", SaveUnsignedIntegerWithCast<unsigned int>);
			AddSaveIntegerFunc("long", SaveIntegerWithCast<long>);
			AddSaveIntegerFunc("unsigned long", SaveUnsignedIntegerWithCast<unsigned long>);
			AddSaveIntegerFunc("long long", SaveIntegerWithCast<long long>);
			AddSaveIntegerFunc("unsigned long long", SaveUnsignedIntegerWithCast<unsigned long long>);

			// Add all decimals
			AddSaveIntegerFunc("float", SaveFloat);
			AddSaveIntegerFunc("double", SaveDouble);

			g_SaveIntegerLookupTableReady = true;
		}
	}


	void SaveType(clutl::DataBuffer& out, const char* object, const clcpp::Type* type)
	{
		unsigned int index = GetSaveIntegerFuncIndex(type->name.hash);
		clcpp::internal::Assert(index < g_HashSaveIntegerMod && "Index is out of range");
		SaveIntegerFunc func = g_SaveIntegerLookupTable[index];
		clcpp::internal::Assert(func && "No save function for type");
		func(out, object);
	}


	void SaveEnum(clutl::DataBuffer& out, const char* object, const clcpp::Enum* enum_type)
	{
		// Do a linear search for an enum with a matching value
		int value = *(int*)object;
		clcpp::Name enum_name;
		for (int i = 0; i < enum_type->constants.size(); i++)
		{
			if (enum_type->constants[i]->value == value)
			{
				enum_name = enum_type->constants[i]->name;
				break;
			}
		}

		// TODO: What if a match can't be found?

		// Write the enum name as the value
		SaveString(out, enum_name.text);
	}


	void SaveClass(clutl::DataBuffer& out, const char* object, const clcpp::Class* class_type)
	{
		out.Write("{", 1);

		// Save each field in the class
		const clcpp::CArray<const clcpp::Field*>& fields = class_type->fields;
		int nb_fields = fields.size();
		for (int i = 0; i < nb_fields; i++)
		{
			const clcpp::Field* field = fields[i];
			const char* field_object = object + field->offset;
			SaveString(out, field->name.text);
			out.Write(":", 1);
			SaveObject(out, field_object, field->type);

			// Comma separator for multiple fields
			if (i < nb_fields - 1)
				out.Write(",", 1);

			out.Write("\n", 1);
		}

		out.Write("}", 1);
	}


	void SaveObject(clutl::DataBuffer& out, const char* object, const clcpp::Type* type)
	{
		// Dispatch to a save function based on kind
		switch (type->kind)
		{
		case (clcpp::Primitive::KIND_TYPE):
			SaveType(out, object, type);
			break;

		case (clcpp::Primitive::KIND_ENUM):
			SaveEnum(out, object, type->AsEnum());
			break;

		case (clcpp::Primitive::KIND_CLASS):
			SaveClass(out, object, type->AsClass());
			break;

		default:
			clcpp::internal::Assert(false && "Invalid primitive kind for type");
		}
	}
}


void clutl::SaveJSON(DataBuffer& out, const void* object, const clcpp::Type* type)
{
	SetupSaveIntegerLookupTable();
	SaveObject(out, (char*)object, type);
}