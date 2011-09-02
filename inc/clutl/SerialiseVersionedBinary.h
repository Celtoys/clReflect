
#pragma once


namespace clcpp
{
	struct Type;
}


namespace clutl
{
	class OutputBuffer;

	void SaveVersionedBinary(OutputBuffer& out, const void* object, const clcpp::Type* type);
}