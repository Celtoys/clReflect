
#pragma once


namespace clcpp
{
	struct Type;
}


namespace clutl
{
	class DataBuffer;

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


	JSONError LoadJSON(DataBuffer& in, void* object, const clcpp::Type* type);
	void SaveJSON(DataBuffer& out, const void* object, const clcpp::Type* type);
}