
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

#include <clutl/Serialise.h>
#include <clcpp/Database.h>


namespace
{
	struct FieldHeader
	{
		// Default construct
		FieldHeader()
			: m_Hash(0)
			, m_DataSize(0)
			, m_WritePosition(0)
		{
		}

		// Construct from a field/type name hash
		FieldHeader(unsigned int hash)
			: m_Hash(hash)
			, m_DataSize(0)
			, m_WritePosition(0)
		{
		}

		void Write(clutl::WriteBuffer& out)
		{
			// Only commit the hash and data size, marking the data size location for future patching
			out.Write(&m_Hash, sizeof(m_Hash));
			m_WritePosition = out.GetBytesWritten();
			out.Write(&m_DataSize, sizeof(m_DataSize));
		}

		void Read(clutl::ReadBuffer& in)
		{
			in.Read(&m_Hash, sizeof(m_Hash));
			in.Read(&m_DataSize, sizeof(m_DataSize));
		}

		void PatchDataSize(clutl::WriteBuffer& out)
		{
			// Calculate size of the data written since the header write and write to the data size field in the header
			m_DataSize = out.GetBytesWritten() - (m_WritePosition + sizeof(unsigned int));
			unsigned int* patch_size = (unsigned int*)(out.GetData() + m_WritePosition);
			*patch_size = m_DataSize;
		}

		unsigned int m_Hash;
		unsigned int m_DataSize;
		unsigned int m_WritePosition;
	};


	void SaveObject(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type, unsigned int hash);
	void LoadObject(clutl::ReadBuffer& in, char* object, const clcpp::Type* type, unsigned int data_size);


	void SaveType(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type)
	{
		out.Write(object, type->size);
	}

	
	void SaveEnum(clutl::WriteBuffer& out, const char* object, const clcpp::Enum* enum_type)
	{
		// Do a linear search for an enum with a matching value
		// TODO: Optimise this to a binary search by storing sorted enum values?
		//       They're currently sorted by name...
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

		// Write the name's has as the value
		out.Write(&enum_name.hash, sizeof(enum_name.hash));
	}


	void SaveClass(clutl::WriteBuffer& out, const char* object, const clcpp::Class* class_type)
	{
		// Save each non-transient field in the class
		const clcpp::CArray<const clcpp::Field*>& fields = class_type->fields;
		for (int i = 0; i < fields.size(); i++)
		{
			const clcpp::Field* field = fields[i];
			if (!(field->flag_attributes & clcpp::FlagAttribute::TRANSIENT))
			{
				const char* field_object = object + field->offset;
				SaveObject(out, field_object, field->type, field->name.hash);
			}
		}
	}


	void LoadClass(clutl::ReadBuffer& in, char* object, const clcpp::Class* class_type, unsigned int data_size)
	{
		// Loop until all the data for this class has been read
		unsigned int end_pos = in.GetBytesRead() + data_size;
		while (in.GetBytesRead() < end_pos)
		{
			// Read the header for this field
			FieldHeader header;
			header.Read(in);

			// If the field exists in the class and it's non-transient, load it
			const clcpp::Field* field = clcpp::FindPrimitive(class_type->fields, header.m_Hash);
			if (field && !(field->flag_attributes & clcpp::FlagAttribute::TRANSIENT))
			{
				char* field_object = object + field->offset;
				LoadObject(in, field_object, field->type, header.m_DataSize);
			}

			// TODO: verify read position + header size
		}
	}


	void SaveObject(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type, unsigned int hash)
	{
		// Write the header
		FieldHeader header(hash);
		header.Write(out);

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

		// Record how much data was written for this field
		header.PatchDataSize(out);
	}


	void LoadType(clutl::ReadBuffer& in, char* object, const clcpp::Type* type)
	{
		in.Read(object, type->size);
	}


	void LoadEnum(clutl::ReadBuffer& in, char* object, const clcpp::Enum* enum_type)
	{
		// Read the enum name hash and do a search for it int he constant list
		unsigned int enum_name_hash;
		in.Read(&enum_name_hash, sizeof(enum_name_hash));
		const clcpp::EnumConstant* constant = clcpp::FindPrimitive(enum_type->constants, enum_name_hash);

		// Copy the enum value if one is found
		if (constant)
			*(int*)object = constant->value;
	}


	void LoadObject(clutl::ReadBuffer& in, char* object, const clcpp::Type* type, unsigned int data_size)
	{
		switch (type->kind)
		{
		case (clcpp::Primitive::KIND_TYPE):
			LoadType(in, object, type);
			break;

		case (clcpp::Primitive::KIND_ENUM):
			LoadEnum(in, object, type->AsEnum());
			break;

		case (clcpp::Primitive::KIND_CLASS):
			LoadClass(in, object, type->AsClass(), data_size);
			break;

		default:
			// Unsupported type
			clcpp::internal::Assert(false);
		}
	}
}

void clutl::SaveVersionedBinary(WriteBuffer& out, const void* object, const clcpp::Type* type)
{
	SaveObject(out, (const char*)object, type, type->name.hash);
}


void clutl::LoadVersionedBinary(ReadBuffer& in, void* object, const clcpp::Type* type)
{
	// Read the header
	FieldHeader header;
	header.Read(in);

	// Type names don't match
	// TODO: Error ?
	if (type->name.hash != header.m_Hash)
		return;

	LoadObject(in, (char*)object, type, header.m_DataSize);

	// TODO: verify position
}