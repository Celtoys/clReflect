
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
}