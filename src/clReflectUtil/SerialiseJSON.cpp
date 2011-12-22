
//
// ===============================================================================
// clReflect
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
// TODO:
//    * Allow names to be specified as CRCs?
//    * Escape sequences need converting.
//    * Enums communicated by value (load integer checks for enum - could have a verify mode to ensure the constant exists).
//    * Field names need to be in memory for JSON serialising to work.
//

#include <clutl/Serialise.h>
#include <clutl/Objects.h>
#include <clcpp/Containers.h>
#include <clcpp/FunctionCall.h>


// Pointers are serialised in hash values in hexadecimal format, which isn't compliant with
// the JSON format.
// Undef this if you want pointers to be serialised as base 10 unsigned integers, instead.
#define SAVE_POINTER_HASH_AS_HEX


// Explicitly stated dependencies in stdlib.h
extern "C" double strtod(const char* s00, char** se);
// This is Microsoft's safer version of the ISO C++ _fcvt which takes the extra buffer and size parameters
// for overflow checking and thread safety
extern "C" int _fcvt_s(char* buffer, unsigned int buffer_size, double val, int count, int* dec, int* sign);


static void SetupTypeDispatchLUT();


namespace
{
	// ----------------------------------------------------------------------------------------------------
	// JSON Lexer
	// ----------------------------------------------------------------------------------------------------


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
		Context(clutl::ReadBuffer& read_buffer)
			: m_ReadBuffer(read_buffer)
			, m_Line(1)
			, m_LinePosition(0)
			, m_StackPosition(0xFFFFFFFF)
		{
		}

		// Consume the given amount of characters in the data buffer, assuming
		// they have been parsed correctly. The original position before the
		// consume operation is returned.
		unsigned int ConsumeChars(int size)
		{
			unsigned int pos = m_ReadBuffer.GetBytesRead();
			m_ReadBuffer.SeekRel(size);
			return pos;
		}
		unsigned int ConsumeChar()
		{
			return ConsumeChars(1);
		}

		// Take a peek at the next N characters in the data buffer
		const char* PeekChars()
		{
			return m_ReadBuffer.ReadAt(m_ReadBuffer.GetBytesRead());
		}
		char PeekChar()
		{
			return *PeekChars();
		}

		// Test to see if reading a specific count of characters would overflow the input
		// data buffer. Automatically sets the error code as a result.
		bool ReadOverflows(int size, clutl::JSONError::Code code = clutl::JSONError::UNEXPECTED_END_OF_DATA)
		{
			if (m_ReadBuffer.GetBytesRead() + size >= m_ReadBuffer.GetTotalBytes())
			{
				SetError(code);
				return true;
			}
			return false;
		}

		// How many bytes are left to parse?
		unsigned int Remaining() const
		{
			return m_ReadBuffer.GetBytesRemaining();
		}

		// Record the first error only, along with its position
		void SetError(clutl::JSONError::Code code)
		{
			if (m_Error.code == clutl::JSONError::NONE)
			{
				m_Error.code = code;
				m_Error.position = m_ReadBuffer.GetBytesRead();
				m_Error.line = m_Line;
				m_Error.column = m_Error.position - m_LinePosition;
			}
		}

		// Increment the current line for error reporting
		void IncLine()
		{
			m_Line++;
			m_LinePosition = m_ReadBuffer.GetBytesRead();
		}

		clutl::JSONError GetError() const
		{
			return m_Error;
		}

		void PushState(const clutl::JSONToken& token)
		{
			clcpp::internal::Assert(m_StackPosition == 0xFFFFFFFF);

			// Push
			m_StackPosition = m_ReadBuffer.GetBytesRead();
			m_StackToken = token;
		}

		void PopState(clutl::JSONToken& token)
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


	clutl::JSONToken LexerString(Context& ctx)
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
			case ('\"'):
				ctx.ConsumeChar();
				return token;

			// Escape sequence
			case ('\\'):
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


	clutl::JSONToken LexerHexInteger(Context& ctx, clutl::JSONToken& token)
	{
		unsigned __int64& uintval = (unsigned __int64&)token.val.integer;

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
			unsigned __int64 digit = 0;
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


	clutl::JSONToken LexerNumber(Context& ctx)
	{
		// Start off construction of an integer
		const char* number_start = ctx.PeekChars();
		clutl::JSONToken token(clutl::JSON_TOKEN_INTEGER, 0);

		// Is this a hex integer?
		unsigned __int64 uintval;
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


	clutl::JSONToken LexerKeyword(Context& ctx, clutl::JSONTokenType type, const char* keyword, int len)
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


	clutl::JSONToken LexerToken(Context& ctx)
	{
	start:
		// Read the current character and return an empty token at stream end
		if (ctx.ReadOverflows(0, clutl::JSONError::NONE))
			return clutl::JSONToken();
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
			return clutl::JSONToken((clutl::JSONTokenType)c, 1);

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
		case ('t'): return LexerKeyword(ctx, clutl::JSON_TOKEN_TRUE, "rue", 3);
		case ('f'): return LexerKeyword(ctx, clutl::JSON_TOKEN_FALSE, "alse", 4);
		case ('n'): return LexerKeyword(ctx, clutl::JSON_TOKEN_NULL, "ull", 3);

		default:
			ctx.SetError(clutl::JSONError::UNEXPECTED_CHARACTER);
			return clutl::JSONToken();
		}
	}


	// ----------------------------------------------------------------------------------------------------
	// Perfect hash based load/save function dispatching
	// ----------------------------------------------------------------------------------------------------


	typedef void (*SaveNumberFunc)(clutl::WriteBuffer&, const char*, unsigned int flags);
	typedef void (*LoadIntegerFunc)(char*, __int64);
	typedef void (*LoadDecimalFunc)(char*, double);


	struct TypeDispatch
	{
		TypeDispatch()
			: valid(false)
			, save_number(0)
			, load_integer(0)
			, load_decimal(0)
		{
		}

		bool valid;
		SaveNumberFunc save_number;
		LoadIntegerFunc load_integer;
		LoadDecimalFunc load_decimal;
	};


	// A save function lookup table for all supported C++ types
	static const int g_TypeDispatchMod = 47;
	TypeDispatch g_TypeDispatchLUT[g_TypeDispatchMod];
	bool g_TypeDispatchLUTReady = false;


	// For the given data set of basic type name hashes, this combines to make
	// a perfect hash function with no collisions, allowing quick indexed lookup.
	unsigned int GetTypeDispatchIndex(unsigned int hash)
	{
		return hash % g_TypeDispatchMod;
	}
	unsigned int GetTypeDispatchIndex(const char* type_name)
	{
		return GetTypeDispatchIndex(clcpp::internal::HashNameString(type_name));
	}


	void AddTypeDispatch(const char* type_name, SaveNumberFunc save_func, LoadIntegerFunc loadi_func, LoadDecimalFunc loadd_func)
	{
		// Ensure there are no collisions before adding the functions
		unsigned index = GetTypeDispatchIndex(type_name);
		clcpp::internal::Assert(g_TypeDispatchLUT[index].valid == false && "Lookup table index collision");
		g_TypeDispatchLUT[index].valid = true;
		g_TypeDispatchLUT[index].save_number = save_func;
		g_TypeDispatchLUT[index].load_integer = loadi_func;
		g_TypeDispatchLUT[index].load_decimal = loadd_func;
	}


	// ----------------------------------------------------------------------------------------------------
	// JSON Parser & reflection-based object construction
	// ----------------------------------------------------------------------------------------------------


	void ParserValue(Context& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op, const clcpp::Field* field);
	void ParserObject(Context& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type);


	clutl::JSONToken Expect(Context& ctx, clutl::JSONToken& t, clutl::JSONTokenType type)
	{
		// Check the tokens match
		if (t.type != type)
		{
			ctx.SetError(clutl::JSONError::UNEXPECTED_TOKEN);
			return clutl::JSONToken();
		}

		// Look-ahead one token
		clutl::JSONToken old = t;
		t = LexerToken(ctx);
		return old;
	}


	void ParserString(const clutl::JSONToken& t, char* object, const clcpp::Type* type)
	{
		// Was there an error expecting a string?
		if (!t.IsValid())
			return;

		// With enum fields, lookup the enum constant by name and assign if it exists
		if (type && type->kind == clcpp::Primitive::KIND_ENUM)
		{
			const clcpp::Enum* enum_type = type->AsEnum();
			unsigned int constant_hash = clcpp::internal::HashData(t.val.string, t.length);
			const clcpp::EnumConstant* constant = clcpp::FindPrimitive(enum_type->constants, constant_hash);
			if (constant)
				*(int*)object = constant->value;
		}
	}


	template <typename TYPE>
	void LoadIntegerWithCast(char* dest, __int64 integer)
	{
		*(TYPE*)dest = (TYPE)integer;
	}
	void LoadIntegerBool(char* dest, __int64 integer)
	{
		*(bool*)dest = integer != 0;
	}


	template <typename TYPE>
	void LoadDecimalWithCast(char* dest, double decimal)
	{
		*(TYPE*)dest = (TYPE)decimal;
	}
	void LoadDecimalBool(char* dest, double decimal)
	{
		*(bool*)dest = decimal != 0;
	}


	void LoadInteger(Context& ctx, __int64 integer, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op)
	{
		if (type == 0)
			return;

		if (op == clcpp::Qualifier::POINTER)
		{
			*(unsigned int*)object = (unsigned int)integer;
		}

		else
		{
			// Dispatch to the correct integer loader based on the field type
			unsigned int index = GetTypeDispatchIndex(type->name.hash);
			clcpp::internal::Assert(index < g_TypeDispatchMod && "Index is out of range");
			LoadIntegerFunc func = g_TypeDispatchLUT[index].load_integer;
			if (func)
				func(object, integer);
		}
	}


	void ParserInteger(Context& ctx, const clutl::JSONToken& t, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op)
	{
		if (t.IsValid())
			LoadInteger(ctx, t.val.integer, object, type, op);
	}


	void ParserDecimal(const clutl::JSONToken& t, char* object, const clcpp::Type* type)
	{
		// Was there an error expecting a decimal?
		if (!t.IsValid())
			return;

		if (type)
		{
			// Dispatch to the correct decimal loader based on the field type
			unsigned int index = GetTypeDispatchIndex(type->name.hash);
			clcpp::internal::Assert(index < g_TypeDispatchMod && "Index is out of range");
			LoadDecimalFunc func = g_TypeDispatchLUT[index].load_decimal;
			if (func)
				func(object, t.val.decimal);
		}
	}


	int ParserElements(Context& ctx, clutl::JSONToken& t, clcpp::WriteIterator* writer, const clcpp::Type* type, clcpp::Qualifier::Operator op)
	{
		// Expect a value first
		if (writer)
			ParserValue(ctx, t, (char*)writer->AddEmpty(), type, op, 0);
		else
			ParserValue(ctx, t, 0, 0, op, 0);

		if (t.type == clutl::JSON_TOKEN_COMMA)
		{
			t = LexerToken(ctx);
			return 1 + ParserElements(ctx, t, writer, type, op);
		}

		return 1;
	}


	void ParserArray(Context& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type, const clcpp::Field* field)
	{
		if (!Expect(ctx, t, clutl::JSON_TOKEN_LBRACKET).IsValid())
			return;

		// Empty array?
		if (t.type == clutl::JSON_TOKEN_RBRACKET)
		{
			t = LexerToken(ctx);
			return;
		}

		clcpp::WriteIterator writer;
		if (field && field->ci)
		{
			// Fields are fixed array iterators
			writer.Initialise(field, object);
		}

		else if (type && type->ci)
		{
			//unsigned int pos = ctx.m_ReadBuffer.GetBytesRead();

			ctx.PushState(t);
			int array_count = ParserElements(ctx, t, 0, 0, clcpp::Qualifier::VALUE);
			ctx.PopState(t);

			// Template types are dynamic container iterators
			writer.Initialise(type->AsTemplateType(), object, array_count);
		}

		if (writer.IsInitialised())
			ParserElements(ctx, t, &writer, writer.m_ValueType, writer.m_ValueIsPtr ? clcpp::Qualifier::POINTER : clcpp::Qualifier::VALUE);
		else
			ParserElements(ctx, t, 0, 0, clcpp::Qualifier::VALUE);

		Expect(ctx, t, clutl::JSON_TOKEN_RBRACKET);
	}


	void ParserLiteralValue(Context& ctx, const clutl::JSONToken& t, int integer, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op)
	{
		if (t.IsValid())
			LoadInteger(ctx, integer, object, type, op);
	}


	void ParserValue(Context& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op, const clcpp::Field* field)
	{
		if (type && type->kind == clcpp::Primitive::KIND_CLASS)
		{
			const clcpp::Class* class_type = type->AsClass();

			// Does this class have a custom load function?
			if (class_type->flag_attributes & clcpp::FlagAttribute::CUSTOM_LOAD)
			{
				// Look it up
				static unsigned int hash = clcpp::internal::HashNameString("load_json");
				if (const clcpp::Attribute* attr = clcpp::FindPrimitive(class_type->attributes, hash))
				{
					const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();

					// Call it and return immediately
					clcpp::CallFunction((clcpp::Function*)name_attr->primitive, clcpp::ByRef(t), object);
					t = LexerToken(ctx);
					return;
				}
			}
		}

		switch (t.type)
		{
		case (clutl::JSON_TOKEN_STRING): return ParserString(Expect(ctx, t, clutl::JSON_TOKEN_STRING), object, type);
		case (clutl::JSON_TOKEN_INTEGER): return ParserInteger(ctx, Expect(ctx, t, clutl::JSON_TOKEN_INTEGER), object, type, op);
		case (clutl::JSON_TOKEN_DECIMAL): return ParserDecimal(Expect(ctx, t, clutl::JSON_TOKEN_DECIMAL), object, type);
		case (clutl::JSON_TOKEN_LBRACE):
			{
				if (type)
					ParserObject(ctx, t, object, type);
				else
					ParserObject(ctx, t, 0, 0);

				Expect(ctx, t, clutl::JSON_TOKEN_RBRACE);
				break;
			}
		case (clutl::JSON_TOKEN_LBRACKET): return ParserArray(ctx, t, object, type, field);
		case (clutl::JSON_TOKEN_TRUE): return ParserLiteralValue(ctx, Expect(ctx, t, clutl::JSON_TOKEN_TRUE), 1, object, type, op);
		case (clutl::JSON_TOKEN_FALSE): return ParserLiteralValue(ctx, Expect(ctx, t, clutl::JSON_TOKEN_FALSE), 0, object, type, op);
		case (clutl::JSON_TOKEN_NULL): return ParserLiteralValue(ctx, Expect(ctx, t, clutl::JSON_TOKEN_NULL), 0, object, type, op);

		default:
			ctx.SetError(clutl::JSONError::UNEXPECTED_TOKEN);
			break;
		}
	}


	const clcpp::Field* FindFieldsRecursive(const clcpp::Type* type, unsigned int hash)
	{
		// Check fields if this is a class
		const clcpp::Field* field = 0;
		if (type->kind == clcpp::Primitive::KIND_CLASS)
			field = clcpp::FindPrimitive(type->AsClass()->fields, hash);

		if (field == 0)
		{
			// Search up through the inheritance hierarchy
			for (int i = 0; i < type->base_types.size(); i++)
			{
				field = FindFieldsRecursive(type->base_types[i], hash);
				if (field)
					break;
			}
		}

		return field;
	}


	void ParserPair(Context& ctx, clutl::JSONToken& t, char*& object, const clcpp::Type*& type)
	{
		// Get the field name
		clutl::JSONToken name = Expect(ctx, t, clutl::JSON_TOKEN_STRING);
		if (!name.IsValid())
			return;

		// Lookup the field in the parent class, if the type is class
		// We want to continue parsing even if there's a mismatch, to skip the invalid data
		const clcpp::Field* field = 0;
		if (type && type->kind == clcpp::Primitive::KIND_CLASS)
		{
			const clcpp::Class* class_type = type->AsClass();
			unsigned int field_hash = clcpp::internal::HashData(name.val.string, name.length);

			field = FindFieldsRecursive(class_type, field_hash);

			// Don't load values for transient fields
			if (field && (field->flag_attributes & clcpp::FlagAttribute::TRANSIENT))
				field = 0;
		}

		if (!Expect(ctx, t, clutl::JSON_TOKEN_COLON).IsValid())
			return;

		// Parse or skip the field if it's unknown
		if (field)
			ParserValue(ctx, t, object + field->offset, field->type, field->qualifier.op, field);
		else
			ParserValue(ctx, t, 0, 0, clcpp::Qualifier::VALUE, 0);
	}


	void ParserMembers(Context& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type)
	{
		ParserPair(ctx, t, object, type);

		// Recurse, parsing more members in the list
		if (t.type == clutl::JSON_TOKEN_COMMA)
		{
			t = LexerToken(ctx);
			ParserMembers(ctx, t, object, type);
		}
	}


	void ParserObject(Context& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type)
	{
		if (!Expect(ctx, t, clutl::JSON_TOKEN_LBRACE).IsValid())
			return;

		// Empty object?
		if (t.type == clutl::JSON_TOKEN_RBRACE)
		{
			t = LexerToken(ctx);
			return;
		}

		ParserMembers(ctx, t, object, type);
	}
}


clutl::JSONError clutl::LoadJSON(ReadBuffer& in, void* object, const clcpp::Type* type)
{
	SetupTypeDispatchLUT();
	Context ctx(in);
	clutl::JSONToken t = LexerToken(ctx);
	ParserObject(ctx, t, (char*)object, type);
	return ctx.GetError();
}


namespace
{
	// ----------------------------------------------------------------------------------------------------
	// JSON text writer using reflected objects
	// ----------------------------------------------------------------------------------------------------


	void SaveObject(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type, unsigned int flags);


	void SaveString(clutl::WriteBuffer& out, const char* start, const char* end)
	{
		out.WriteChar('\"');
		out.Write(start, end - start);
		out.WriteChar('\"');
	}


	void SaveString(clutl::WriteBuffer& out, const char* str)
	{
		out.WriteChar('\"');
		out.WriteStr(str);
		out.WriteChar('\"');
	}


	void SaveInteger(clutl::WriteBuffer& out, __int64 integer)
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


	void SaveUnsignedInteger(clutl::WriteBuffer& out, unsigned __int64 integer)
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
			unsigned __int64 next_integer = integer / 10;
			*--tptr = char('0' + (integer - next_integer * 10));
			integer = next_integer;
		} while (integer);

		out.Write(tptr, end - tptr);
	}


	void SaveHexInteger(clutl::WriteBuffer& out, unsigned __int64 integer)
	{
		// Enough to store a 64-bit int + null
		static const int MAX_SZ = 21;
		char text[MAX_SZ];

		// Null terminate and start at the end
		text[MAX_SZ - 1] = 0;
		char* end = text + MAX_SZ - 1;
		char* tptr = end;

		// Loop through the value with radix 16
		do 
		{
			unsigned __int64 next_integer = integer / 16;
			*--tptr = "0123456789ABCDEF"[integer - next_integer * 16];
			integer = next_integer;
		} while (integer);

		out.Write(tptr, end - tptr);
	}


	// Automate the process of de-referencing an object as its exact type so no
	// extra bytes are read incorrectly and values are correctly zero-extended.
	template <typename TYPE>
	void SaveIntegerWithCast(clutl::WriteBuffer& out, const char* object, unsigned int)
	{
		SaveInteger(out, *(TYPE*)object);
	}
	template <typename TYPE>
	void SaveUnsignedIntegerWithCast(clutl::WriteBuffer& out, const char* object, unsigned int)
	{
		SaveUnsignedInteger(out, *(TYPE*)object);
	}


	void SaveDecimal(clutl::WriteBuffer& out, double decimal, unsigned int flags)
	{
		if (flags & clutl::JSONFlags::EMIT_HEX_FLOATS)
		{
			// Use a specific prefix to inform the lexer to alias as a decimal
			out.WriteStr("0d");
			SaveHexInteger(out, (unsigned __int64&)decimal);
			return;
		}

		// Convert the double to a string on the local stack
		char fcvt_buffer[512] = { 0 };
		int dec = -1, sign = -1;
		_fcvt_s(fcvt_buffer, sizeof(fcvt_buffer), decimal, 48, &dec, &sign);

		char decimal_buffer[512];
		char* iptr = fcvt_buffer;
		char* optr = decimal_buffer;

		// Prefix with negative sign
		if (sign)
			*optr++ = '-';

		// With a negative decimal position, prefix with 0. and however many
		// zeroes are needed
		if (dec <= 0)
		{
			*optr++ = '0';
			*optr++ = '.';

			dec = -dec;
			while (dec-- > 0)
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


	void SaveDouble(clutl::WriteBuffer& out, const char* object, unsigned int flags)
	{
		SaveDecimal(out, *(double*)object, flags);
	}
	void SaveFloat(clutl::WriteBuffer& out, const char* object, unsigned int flags)
	{
		SaveDecimal(out, *(float*)object, flags);
	}


	void SaveType(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type, unsigned int flags)
	{
		unsigned int index = GetTypeDispatchIndex(type->name.hash);
		clcpp::internal::Assert(index < g_TypeDispatchMod && "Index is out of range");
		SaveNumberFunc func = g_TypeDispatchLUT[index].save_number;
		clcpp::internal::Assert(func && "No save function for type");
		func(out, object, flags);
	}


	void SaveEnum(clutl::WriteBuffer& out, const char* object, const clcpp::Enum* enum_type)
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


	bool IsNamedObjectPtr(const void* object, unsigned int& hash)
	{
		// Only use the hash if the pointer is non-null
		clutl::Object* named_object = *((clutl::Object**)object);
		hash = 0;
		if (named_object != 0)
		{
			hash = named_object->name.hash;

			// If the target object has no name then its pointer is not meant for serialisation
			if (hash == 0)
				return false;
		}

		return true;
	}


	void SavePtr(clutl::WriteBuffer& out, unsigned int hash)
	{
#ifdef SAVE_POINTER_HASH_AS_HEX
		out.WriteStr("0x");
		SaveHexInteger(out, hash);
#else
		SaveUnsignedInteger(out, hash);
#endif
	}


	bool SavePtr(clutl::WriteBuffer& out, const void* object, const clcpp::Type* type, int backtrack_on_failure)
	{
		if (type->kind == clcpp::Primitive::KIND_CLASS)
		{
			const clcpp::Class* class_type = type->AsClass();

			// Only save pointer types that derive from Object and are named
			unsigned int hash = 0;
			if (class_type->DerivesFrom(clcpp::GetTypeNameHash<clutl::Object>()) &&
				IsNamedObjectPtr(object, hash))
			{
				SavePtr(out, hash);
				return true;
			}
		}

		out.SeekRel(-backtrack_on_failure);
		return false;
	}


	void SaveContainer(clutl::WriteBuffer& out, clcpp::ReadIterator& reader, unsigned int flags)
	{
		// TODO: If the iterator has a key, save a dictionary instead

		out.WriteChar('[');

		// Figure out if this an iterator over named object pointers
		if (reader.m_ValueIsPtr)
		{
			if (reader.m_ValueType->kind == clcpp::Primitive::KIND_CLASS)
			{
				const clcpp::Class* class_type = reader.m_ValueType->AsClass();
				if (class_type->DerivesFrom(clcpp::GetTypeNameHash<clutl::Object>()))
				{
					// Save comma-separated pointers
					bool written = false;
					for (unsigned int i = 0; i < reader.m_Count; i++)
					{
						clcpp::ContainerKeyValue kv = reader.GetKeyValue();

						// Only save if the object is named
						unsigned int hash = 0;
						if (IsNamedObjectPtr(kv.value, hash))
						{
							if (written)
								out.WriteChar(',');
							SavePtr(out, hash);
							written = true;
						}

						reader.MoveNext();
					}
				}
			}
		}

		else
		{
			// Save comma-separated objects
			for (unsigned int i = 0; i < reader.m_Count - 1; i++)
			{
				clcpp::ContainerKeyValue kv = reader.GetKeyValue();
				SaveObject(out, (char*)kv.value, reader.m_ValueType, flags);
				out.WriteChar(',');
				reader.MoveNext();
			}

			// Save the final object without a comma
			clcpp::ContainerKeyValue kv = reader.GetKeyValue();
			SaveObject(out, (char*)kv.value, reader.m_ValueType, flags);
		}

		out.WriteChar(']');
	}


	void SaveFieldArray(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, unsigned int flags)
	{
		// Construct a read iterator and leave early if there are no elements
		clcpp::ReadIterator reader(field, object);
		if (reader.m_Count == 0)
		{
			out.WriteStr("[]");
			return;
		}

		SaveContainer(out, reader, flags);
	}


	inline void NewLine(clutl::WriteBuffer& out, unsigned int flags)
	{
		if (flags & clutl::JSONFlags::FORMAT_OUTPUT)
		{
			out.WriteChar('\n');

			// Open the next new line with tabs
			for (unsigned int i = 0; i < (flags & clutl::JSONFlags::INDENT_MASK); i++)
				out.WriteChar('\t');
		}
	}


	inline void OpenScope(clutl::WriteBuffer& out, unsigned int& flags)
	{
		if (flags & clutl::JSONFlags::FORMAT_OUTPUT)
		{
			NewLine(out, flags);
			out.WriteChar('{');
			
			// Increment indent level
			int indent_level = flags & clutl::JSONFlags::INDENT_MASK;
			flags &= ~clutl::JSONFlags::INDENT_MASK;
			flags |= ((indent_level + 1) & clutl::JSONFlags::INDENT_MASK);

			NewLine(out, flags);
		}
		else
		{
			out.WriteChar('{');
		}
	}


	inline void CloseScope(clutl::WriteBuffer& out, unsigned int& flags)
	{
		if (flags & clutl::JSONFlags::FORMAT_OUTPUT)
		{
			// Decrement indent level
			int indent_level = flags & clutl::JSONFlags::INDENT_MASK;
			flags &= ~clutl::JSONFlags::INDENT_MASK;
			flags |= ((indent_level - 1) & clutl::JSONFlags::INDENT_MASK);

			NewLine(out, flags);
			out.WriteChar('}');
			NewLine(out, flags);
		}
		else
		{
			out.WriteChar('}');
		}
	}


	void SaveClassFields(clutl::WriteBuffer& out, const char* object, const clcpp::Class* class_type, unsigned int& flags, bool& field_written)
	{
		// Save each field in the class
		const clcpp::CArray<const clcpp::Field*>& fields = class_type->fields;
		int nb_fields = fields.size();
		for (int i = 0; i < nb_fields; i++)
		{
			// Don't save values for transient fields
			const clcpp::Field* field = fields[i];
			if (field->flag_attributes & clcpp::FlagAttribute::TRANSIENT)
				continue;

			// Comma separator for multiple fields
			int field_start_pos = out.GetBytesWritten();
			if (field_written)
			{
				out.WriteChar(',');
				NewLine(out, flags);
			}

			// Write the field name
			SaveString(out, field->name.text);
			out.WriteChar(':');

			// Dispatch to save function that can handle the field type
			bool success = true;
			const char* field_object = object + field->offset;
			if (field->ci != 0)
				SaveFieldArray(out, field_object, field, flags);
			else if (field->qualifier.op == clcpp::Qualifier::POINTER)
				success = SavePtr(out, field_object, field->type, out.GetBytesWritten() - field_start_pos);
			else
				SaveObject(out, field_object, field->type, flags);

			if (success)
				field_written = true;
		}

	}


	void SaveClass(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type, unsigned int& flags, bool& field_written)
	{
		// Save body of the class
		if (type->kind == clcpp::Primitive::KIND_CLASS)
			SaveClassFields(out, object, type->AsClass(), flags, field_written);

		// Recurse into base types
		for (int i = 0; i < type->base_types.size(); i++)
		{
			const clcpp::Type* base_type = type->base_types[i];
			SaveClass(out, object, base_type, flags, field_written);
		}
	}


	void SaveClass(clutl::WriteBuffer& out, const char* object, const clcpp::Class* class_type, unsigned int flags)
	{
		// Is there a custom loading function for this class?
		if (class_type->flag_attributes & clcpp::FlagAttribute::CUSTOM_SAVE)
		{
			// Look it up
			static unsigned int hash = clcpp::internal::HashNameString("save_json");
			if (const clcpp::Attribute* attr = clcpp::FindPrimitive(class_type->attributes, hash))
			{
				const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();

				// Call the function to generate an output token
				clutl::JSONToken token;
				clcpp::CallFunction((clcpp::Function*)name_attr->primitive, clcpp::ByRef(token), object);

				// Serialise appropriately
				switch (token.type)
				{
				case clutl::JSON_TOKEN_STRING:
					SaveString(out, token.val.string, token.val.string + token.length);
					break;
				case clutl::JSON_TOKEN_INTEGER:
					SaveInteger(out, token.val.integer);
					break;
				case clutl::JSON_TOKEN_DECIMAL:
					SaveDecimal(out, token.val.decimal, flags);
					break;
				default:
					clcpp::internal::Assert(false && "Invalid token output type");
				}

				return;
			}
		}

		bool field_written = false;
		OpenScope(out, flags);
		SaveClass(out, object, class_type, flags, field_written);
		CloseScope(out, flags);
	}


	void SaveTemplateType(clutl::WriteBuffer& out, const char* object, const clcpp::TemplateType* template_type, unsigned int flags)
	{
		// Construct a read iterator and leave early if there are no elements
		clcpp::ReadIterator reader(template_type, object);
		if (reader.m_Count == 0)
		{
			out.WriteStr("[]");
			return;
		}

		SaveContainer(out, reader, flags);
	}


	void SaveObject(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type, unsigned int flags)
	{
		// Dispatch to a save function based on kind
		switch (type->kind)
		{
		case (clcpp::Primitive::KIND_TYPE):
			SaveType(out, object, type, flags);
			break;

		case (clcpp::Primitive::KIND_ENUM):
			SaveEnum(out, object, type->AsEnum());
			break;

		case (clcpp::Primitive::KIND_CLASS):
			SaveClass(out, object, type->AsClass(), flags);
			break;

		case (clcpp::Primitive::KIND_TEMPLATE_TYPE):
			SaveTemplateType(out, object, type->AsTemplateType(), flags);
			break;

		default:
			clcpp::internal::Assert(false && "Invalid primitive kind for type");
		}
	}
}


void clutl::SaveJSON(WriteBuffer& out, const void* object, const clcpp::Type* type, unsigned int flags)
{
	SetupTypeDispatchLUT();
	SaveObject(out, (char*)object, type, flags);
}


static void SetupTypeDispatchLUT()
{
	if (!g_TypeDispatchLUTReady)
	{
		// Add all integers
		AddTypeDispatch("bool", SaveIntegerWithCast<bool>, LoadIntegerBool, LoadDecimalBool);
		AddTypeDispatch("char", SaveIntegerWithCast<char>, LoadIntegerWithCast<char>, LoadDecimalWithCast<char>);
		AddTypeDispatch("wchar_t", SaveUnsignedIntegerWithCast<wchar_t>, LoadIntegerWithCast<wchar_t>, LoadDecimalWithCast<wchar_t>);
		AddTypeDispatch("unsigned char", SaveUnsignedIntegerWithCast<unsigned char>, LoadIntegerWithCast<unsigned char>, LoadDecimalWithCast<unsigned char>);
		AddTypeDispatch("short", SaveIntegerWithCast<short>, LoadIntegerWithCast<short>, LoadDecimalWithCast<short>);
		AddTypeDispatch("unsigned short", SaveUnsignedIntegerWithCast<unsigned short>, LoadIntegerWithCast<unsigned short>, LoadDecimalWithCast<unsigned short>);
		AddTypeDispatch("int", SaveIntegerWithCast<int>, LoadIntegerWithCast<int>, LoadDecimalWithCast<int>);
		AddTypeDispatch("unsigned int", SaveUnsignedIntegerWithCast<unsigned int>, LoadIntegerWithCast<unsigned int>, LoadDecimalWithCast<unsigned int>);
		AddTypeDispatch("long", SaveIntegerWithCast<long>, LoadIntegerWithCast<long>, LoadDecimalWithCast<long>);
		AddTypeDispatch("unsigned long", SaveUnsignedIntegerWithCast<unsigned long>, LoadIntegerWithCast<unsigned long>, LoadDecimalWithCast<unsigned long>);
		AddTypeDispatch("long long", SaveIntegerWithCast<long long>, LoadIntegerWithCast<long long>, LoadDecimalWithCast<long long>);
		AddTypeDispatch("unsigned long long", SaveUnsignedIntegerWithCast<unsigned long long>, LoadIntegerWithCast<unsigned long long>, LoadDecimalWithCast<unsigned long long>);

		// Add all decimals
		AddTypeDispatch("float", SaveFloat, LoadIntegerWithCast<float>, LoadDecimalWithCast<float>);
		AddTypeDispatch("double", SaveDouble, LoadIntegerWithCast<double>, LoadDecimalWithCast<double>);

		g_TypeDispatchLUTReady = true;
	}
}


