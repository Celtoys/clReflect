
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clutl/Serialise.h>
#include <clcpp/FunctionCall.h>
#include <clcpp/Containers.h>


namespace
{
	struct ChunkHeader
	{
		// Construct from a field
		ChunkHeader(unsigned int type_hash, unsigned int name_hash)
			: type_hash(type_hash)
			, name_hash(name_hash)
			, data_size(0)
		{
		}

		// Construct from a read buffer
		ChunkHeader(clutl::ReadBuffer& in)
			: type_hash(0)
			, name_hash(0)
			, data_size(0)
		{
			in.Read(&type_hash, sizeof(type_hash));
			in.Read(&name_hash, sizeof(name_hash));
			in.Read(&data_size, sizeof(data_size));
		}

		// Expected type and name expressed as hashes
		unsigned int type_hash;
		unsigned int name_hash;

		// Total size of the chunk, for skipping unknown data
		unsigned int data_size;
	};


	struct SizeBackPatcher
	{
		SizeBackPatcher()
			: size_offset(0xFFFFFFFF)
		{
		}

		void Mark(clutl::WriteBuffer& out)
		{
			// Mark the location of the patch and write a dummy value
			size_offset = out.GetBytesWritten();
			unsigned int zero = 0;
			out.Write(&zero, sizeof(zero));
		}

		unsigned int Patch(clutl::WriteBuffer& out)
		{
			if (size_offset == 0xFFFFFFFF)
				return 0;

			// Calculate size of the data written since the mark and write to the data size offset
			unsigned int size = out.GetBytesWritten() - (size_offset + sizeof(unsigned int));
			unsigned int* patch_size = (unsigned int*)(out.GetData() + size_offset);
			*patch_size = size;
			return size;
		}

		// Position of the size for back-patching
		unsigned int size_offset;
	};


	struct ChunkHeaderWriter
	{
		ChunkHeaderWriter(clutl::WriteBuffer& out, unsigned int type_hash, unsigned int name_hash)
			: out(out)
			, header(type_hash, name_hash)
		{
			// Only commit the hashes and data size, marking the data size location for future patching
			out.Write(&header.type_hash, sizeof(header.type_hash));
			out.Write(&header.name_hash, sizeof(header.name_hash));
			patcher.Mark(out);
		}

		~ChunkHeaderWriter()
		{
			header.data_size = patcher.Patch(out);
		}

		clutl::WriteBuffer& out;

		ChunkHeader header;

		SizeBackPatcher patcher;
	};


	struct ContainerChunkHeader
	{
		ContainerChunkHeader(clutl::WriteBuffer& out, clcpp::ReadIterator& reader)
			: count(reader.m_Count)
			, value_type_hash(reader.m_ValueType->name.hash)
			, value_type_size(0)
		{
			if (reader.m_ValueIsPtr)
			{
				// Pointers are fixed size
				value_type_size = sizeof(void*);
			}
			else if (reader.m_ValueType->kind == clcpp::Primitive::KIND_CLASS)
			{
				// Classes potentially have variable size that needs to be
				// stored with each entry
				value_type_size = 0;
			}
			else
			{
				// Everything else can trust the type size
				value_type_size = reader.m_ValueType->size;
			}

			// Immediately write values out
			out.Write(&count, sizeof(count));
			out.Write(&value_type_hash, sizeof(value_type_hash));
			out.Write(&value_type_size, sizeof(value_type_size));
		}

		ContainerChunkHeader(clutl::ReadBuffer& in)
		{
			// Read values for later use
			in.Read(&count, sizeof(count));
			in.Read(&value_type_hash, sizeof(value_type_hash));
			in.Read(&value_type_size, sizeof(value_type_size));
		}

		unsigned int count;

		unsigned int value_type_hash;

		unsigned int value_type_size;
	};


	void SaveObject(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type);
	void LoadObject(clutl::ReadBuffer& in, char* object, const clcpp::Type* type, unsigned int data_size, unsigned int type_hash);


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
		for (unsigned int i = 0; i < enum_type->constants.size; i++)
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


	void SaveContainer(clutl::WriteBuffer& out, clcpp::ReadIterator& reader)
	{
		// Add the container header
		ContainerChunkHeader header(out, reader);

		for (unsigned int i = 0; i < reader.m_Count; i++)
		{
			clcpp::ContainerKeyValue kv = reader.GetKeyValue();

			// If this is a value that could have variable data written, store a size next to it
			SizeBackPatcher patcher;
			if (header.value_type_size == 0)
				patcher.Mark(out);

			if (reader.m_ValueIsPtr)
			{
				// Ask the user if they want to save this pointer
				//void* ptr = *(void**)kv.value;
				//if (ptr_save == 0 || !ptr_save->CanSavePtr(ptr, field, reader.m_ValueType))
				//	continue;

				//SavePtr(out, kv.value, ptr_save, flags);
			}
			else
			{
				SaveObject(out, (char*)kv.value, reader.m_ValueType);
			}

			// Patch any accompanying sizes
			patcher.Patch(out);

			reader.MoveNext();
		}
	}


	void SaveFieldArray(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field)
	{
		// Construct a read iterator and serialise as container
        clcpp::ReadIterator reader;
        reader.Initialise(field, object);
        SaveContainer(out, reader);
	}


	void SaveClassField(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field)
	{
        	if ((field->flag_attributes & attrFlag_Transient) != 0)
		{
			return;
		}

		ChunkHeaderWriter header_writer(out, field->type->name.hash, field->name.hash);

		// Is there a custom save function for this field?
		// TODO: Flag for marking custom saves on a field
		if (field->attributes.size != 0)
		{
			static unsigned int hash = clcpp::internal::HashNameString("save_vbin");			
			if (const clcpp::Attribute* attr = clcpp::FindPrimitive(field->attributes, hash))
			{
				// Call the function to write data
				const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();
				clcpp::CallFunction((clcpp::Function*)name_attr->primitive, clcpp::ByRef(out), object);
				return;
			}
		}

		// ContainerInfos for fields can only be C-Arrays
		if (field->ci != 0)
			SaveFieldArray(out, object, field);
		else
			SaveObject(out, object, field->type);
	}


	void SaveClass(clutl::WriteBuffer& out, const char* object, const clcpp::Class* class_type)
	{
		// Save each field in the class
		const clcpp::CArray<const clcpp::Field*>& fields = class_type->fields;
		for (unsigned int i = 0; i < fields.size; i++)
		{
			const clcpp::Field* field = fields[i];
			const char* field_object = object + field->offset;
			SaveClassField(out, field_object, field);
		}
	}


	void SaveObject(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type)
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


	void LoadType(clutl::ReadBuffer& in, char* object, const clcpp::Type* type, unsigned int data_size)
	{
		// Primitive data types must be the same size, for now. I guess this can only happen when sharing
		// data files between platforms.
		// TODO: Conversion of same-named data types of varying size?
		if (type->size != data_size)
			// TODO: Mark error
			in.SeekRel(data_size);
		else
			in.Read(object, type->size);
	}


	void LoadEnum(clutl::ReadBuffer& in, char* object, const clcpp::Enum* enum_type, unsigned int data_size)
	{
		// Enum data types must be the same size, for now
		// I guess this is more possible than primitive data types as enum size can change between compiles
		// TODO: Conversion of variable-sized enums
		if (enum_type->size != data_size)
		{
			// TODO: Mark error
			in.SeekRel(data_size);
			return;
		}

		// Read the enum name hash and do a search for it in the constant list
		unsigned int enum_name_hash;
		in.Read(&enum_name_hash, sizeof(enum_name_hash));
		const clcpp::EnumConstant* constant = clcpp::FindPrimitive(enum_type->constants, enum_name_hash);

		// Copy the enum value if one is found
		if (constant)
			*(int*)object = constant->value;
	}


	void LoadContainer(clutl::ReadBuffer& in, clcpp::WriteIterator& writer, unsigned int data_size, unsigned int expected_count)
	{
		unsigned int end_pos = in.GetBytesRead() + data_size;

		ContainerChunkHeader header(in);

		// Check array ranges
		unsigned int count = header.count;
		if (expected_count > count)
		{
			// This is fine, there's less data to load than storage space available
			// TODO: Warning?
		}
		else if (expected_count < count)
		{
			// This is not fine, there's more data to load than storage space available
			// Load as much as possible
			// TODO: Warning
			count = expected_count;
		}

		// Ensure value types match
		if (header.value_type_hash != writer.m_ValueType->name.hash)
		{
			// TODO: Warning
			in.SeekRel(data_size);
			return;
		}

		for (unsigned int i = 0; i < count; i++)
		{
			char* container_object = (char*)writer.AddEmpty();

			// Check to see if this is a value type that may be variable size
			unsigned int value_type_size = header.value_type_size;
			if (value_type_size == 0)
				in.Read(&value_type_size, sizeof(value_type_size));

			if (writer.m_ValueIsPtr)
			{
				// Ask the user if they want to save this pointer
				//void* ptr = *(void**)kv.value;
				//if (ptr_save == 0 || !ptr_save->CanSavePtr(ptr, field, reader.m_ValueType))
				//	continue;

				//SavePtr(out, kv.value, ptr_save, flags);
			}
			else
			{
				LoadObject(in, container_object, writer.m_ValueType, value_type_size, header.value_type_hash);
			}
		}

		int bytes_left = end_pos - in.GetBytesRead();
		if (bytes_left > 0)
		{
			if (header.count != count)
			{
				// Skip over remaining data for when expected_count < count
				in.SeekRel(bytes_left);
			}

			else
			{
				// ERROR: Lower levels should catch and correct underflow
				// Assert?
			}
		}
		else if (bytes_left < 0)
		{
			// ERROR: Lower levels should catch and correct overflow
			// Assert?
		}
	}


	void LoadFieldArray(clutl::ReadBuffer& in, char* object, const clcpp::Field* field, unsigned int data_size)
	{
		// Create an array write iterator
		clcpp::WriteIterator writer;
		writer.Initialise(field, object);
		LoadContainer(in, writer, data_size, field->ci->count);
	}


	void LoadClassField(clutl::ReadBuffer& in, char* object, const clcpp::Class* class_type)
	{
		// Read the header and skip the chunk if the field doesn't exist or its destination is transient
		ChunkHeader header(in);
		const clcpp::Field* field = clcpp::FindPrimitive(class_type->fields, header.name_hash);
        if (field == nullptr || (field->flag_attributes & attrFlag_Transient) != 0)
		{
			in.SeekRel(header.data_size);
			return;
		}
		char* field_object = object + field->offset;

		// Is there a custom load function for this field?
		// TODO: Flag for marking custom loads on a field
		if (field->attributes.size != 0)
		{
			static unsigned int hash = clcpp::internal::HashNameString("load_vbin");			
			if (const clcpp::Attribute* attr = clcpp::FindPrimitive(field->attributes, hash))
			{
				int end_pos = in.GetBytesRead() + header.data_size;

				// Call the function to read the data
				const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();
				clcpp::CallFunction((clcpp::Function*)name_attr->primitive, clcpp::ByRef(in), field_object);

				// Correct any read errors in the custom function
				int position = in.GetBytesRead();
				if (position < end_pos)
				{
					// TODO: Warning, not enough data read by custom reader
					in.SeekRel(end_pos - position);
				}
				else if (position > end_pos)
				{
					// TODO: Warning, too much data read by custom reader
					in.SeekRel(end_pos - position);
				}

				return;
			}
		}

		if (field->ci != 0)
		{
			// TODO: What happens if counts differ?
			LoadFieldArray(in, field_object, field, header.data_size);
		}
		else
		{
			LoadObject(in, field_object, field->type, header.data_size, header.type_hash);
		}
	}


	void LoadClass(clutl::ReadBuffer& in, char* object, const clcpp::Class* class_type, unsigned int data_size)
	{
		// Loop until all the data for this class has been read
		unsigned int end_pos = in.GetBytesRead() + data_size;
		while (in.GetBytesRead() < end_pos)
		{
			LoadClassField(in, object, class_type);
		}

		if (in.GetBytesRead() != end_pos)
		{
			// TODO: Error! More than an internal error, as long as custom fields are read correctly
			// TODO: Check custom field reads
		}
	}


	void LoadObject(clutl::ReadBuffer& in, char* object, const clcpp::Type* type, unsigned int data_size, unsigned int type_hash)
	{
		// If the header type doesn't match the expected type, skip this object
		// TODO: If types are not equal, are they convertible?
		if (type_hash != type->name.hash)
		{
			in.SeekRel(data_size);
			return;
		}

		switch (type->kind)
		{
		case (clcpp::Primitive::KIND_TYPE):
			LoadType(in, object, type, data_size);
			break;

		case (clcpp::Primitive::KIND_ENUM):
			LoadEnum(in, object, type->AsEnum(), data_size);
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

CLCPP_API void clutl::SaveVersionedBinary(WriteBuffer& out, const void* object, const clcpp::Type* type)
{
	ChunkHeaderWriter header_writer(out, type->name.hash, 0);
	SaveObject(out, (const char*)object, type);
}


CLCPP_API void clutl::LoadVersionedBinary(ReadBuffer& in, void* object, const clcpp::Type* type)
{
	ChunkHeader header(in);
	LoadObject(in, (char*)object, type, header.data_size, header.type_hash);

	// TODO: verify position
}