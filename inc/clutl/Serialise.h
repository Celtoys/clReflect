
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
			ERROR_NONE,
			ERROR_LEXER_UNEXPECTED_END,
			ERROR_PARSER_UNEXPECTED_TOKEN,
		};

		Code error;
	};


	void LoadJSON(DataBuffer& in, void* object, const clcpp::Type* type);
}