
#include <clutl/SerialiseVersionedBinary.h>
#include <clutl/Containers.h>
#include <clcpp/Database.h>


namespace
{
	class FieldHeader
	{
	public:
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

		void Write(clutl::OutputBuffer& out)
		{
			// Only commit the hash and data size, marking the data size location for future patching
			out.Write(&m_Hash, sizeof(m_Hash));
			m_WritePosition = out.GetPosition();
			out.Write(&m_DataSize, sizeof(m_DataSize));
		}

		void PatchDataSize(clutl::OutputBuffer& out)
		{
			// Calculate size of the data written since the header write and write to the data size field in the header
			m_DataSize = out.GetPosition() - (m_WritePosition + sizeof(unsigned int));
			out.WriteAt(&m_DataSize, sizeof(m_DataSize), m_WritePosition);
		}

	private:
		unsigned int m_Hash;
		unsigned int m_DataSize;
		unsigned int m_WritePosition;
	};


	void SaveObject(clutl::OutputBuffer& out, const char* object, const clcpp::Type* type, unsigned int hash);


	void SaveType(clutl::OutputBuffer& out, const char* object, const clcpp::Type* type)
	{
		out.Write(object, type->size);
	}


	void SaveEnum(clutl::OutputBuffer& out, const char* object, const clcpp::Enum* enum_type)
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


	void SaveClass(clutl::OutputBuffer& out, const char* object, const clcpp::Class* class_type)
	{
		// Save each field in the class
		const clcpp::CArray<const clcpp::Field*>& fields = class_type->fields;
		for (int i = 0; i < fields.size(); i++)
		{
			const clcpp::Field* field = fields[i];
			const char* field_object = object + field->offset;
			SaveObject(out, field_object, field->type, field->name.hash);
		}
	}


	void SaveObject(clutl::OutputBuffer& out, const char* object, const clcpp::Type* type, unsigned int hash)
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
}

void clutl::SaveVersionedBinary(OutputBuffer& out, const void* object, const clcpp::Type* type)
{
	SaveObject(out, (const char*)object, type, type->name.hash);
}