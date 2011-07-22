
#include <crcpp\Database.h>

#include <cstdio>
#include <cstring>


namespace
{
	// Header information
	const unsigned char DATABASE_SIGNATURE[] = "crcppdb";
	const unsigned int DATABASE_VERSION = 1;


	struct ReadContext
	{
		ReadContext()
			: nb_primitives(0)
			, primitives(0)
			, name_data_size(0)
			, name_data(0)
		{
		}


		void SetPrimitive(int index, const crcpp::Primitive* primitive) const
		{
			// TODO: assert range
			primitives[index] = primitive;
		}

		const char* GetText(int offset) const
		{
			// TODO: assert range
			return name_data + offset;
		}

		int nb_primitives;
		const crcpp::Primitive** primitives;

		int name_data_size;
		const char* name_data;
	};


	template <typename TYPE> void Read(const ReadContext& ctx, TYPE& dest, FILE* fp)
	{
		// Anything with no overload of Read is a straight POD read
		fread(&dest, sizeof(dest), 1, fp);
	}


	template <typename TYPE> TYPE Read(const ReadContext& ctx, FILE* fp)
	{
		TYPE temp;
		Read(ctx, temp, fp);
		return temp;
	}


	template <typename TYPE> void ReadArray(const ReadContext& ctx, crcpp::ConstArray<TYPE>& dest, FILE* fp)
	{
		// Reads primitives that are stored inline next to each other
		// Has to cast away const-ness to write while maintaining a const public API
		int size = Read<int>(ctx, fp);
		dest = crcpp::ConstArray<TYPE>(size);
		for (int i = 0; i < size; i++)
		{
			TYPE& val = const_cast<TYPE&>(dest[i]);
			Read(ctx, val, fp);
		}
	}


	template <typename TYPE> void ReadPrimitivePtr(const ReadContext& ctx, const TYPE*& dest, FILE* fp)
	{
		// Ensure this is a primitive pointer
		static_cast<const crcpp::Primitive*>(dest);

		// Primitive pointer is aliased as integer type for storing the index in the global primitive array
		Read(ctx, (unsigned int&)dest, fp);
	}


	template <typename TYPE> void ReadPrimitivePtrArray(const ReadContext& ctx, crcpp::ConstArray<const TYPE*>& dest, FILE* fp)
	{
		// Reads an array of primitive pointers stored as hash values
		// Has to cast away const-ness to write while maintaining a const public API
		int size = Read<int>(ctx, fp);
		dest = crcpp::ConstArray<const TYPE*>(size);
		for (int i = 0; i < size; i++)
		{
			const TYPE*& ptr = const_cast<const TYPE*&>(dest[i]);
			ReadPrimitivePtr(ctx, ptr, fp);
		}
	}


	template <> void Read(const ReadContext& ctx, crcpp::Name& dest, FILE* fp)
	{
		// Read the unique hash ID and link in the text name given the table offset
		Read(ctx, dest.hash, fp);
		int offset = Read<int>(ctx, fp);
		dest.text = ctx.GetText(offset);
	}


	template <> void Read(const ReadContext& ctx, crcpp::Primitive& dest, FILE* fp)
	{
		// The primitive table index is pre-calculated so add the pointer immediately
		int index = Read<int>(ctx, fp);
		ctx.SetPrimitive(index, &dest);

		Read(ctx, dest.kind, fp);
		Read(ctx, dest.name, fp);
		ReadPrimitivePtr(ctx, dest.parent, fp);
	}


	template <> void Read(const ReadContext& ctx, crcpp::Type& dest, FILE* fp)
	{
		Read(ctx, (crcpp::Primitive&)dest, fp);
	}


	template <> void Read(const ReadContext& ctx, crcpp::EnumConstant& dest, FILE* fp)
	{
		Read(ctx, (crcpp::Primitive&)dest, fp);
		Read(ctx, dest.value, fp);
	}


	template <> void Read(const ReadContext& ctx, crcpp::Enum& dest, FILE* fp)
	{
		Read(ctx, (crcpp::Type&)dest, fp);
		ReadArray(ctx, dest.constants, fp);
	}


	template <> void Read(const ReadContext& ctx, crcpp::Field& dest, FILE* fp)
	{
		Read(ctx, (crcpp::Primitive&)dest, fp);
		ReadPrimitivePtr(ctx, dest.type, fp);
		Read(ctx, dest.modifier, fp);
		Read(ctx, dest.is_const, fp);
		Read(ctx, dest.offset, fp);
	}


	template <> void Read(const ReadContext& ctx, crcpp::Function& dest, FILE* fp)
	{
		Read(ctx, (crcpp::Primitive&)dest, fp);
		Read(ctx, dest.return_parameter, fp);
		ReadArray(ctx, dest.parameters, fp);
	}


	template <> void Read(const ReadContext& ctx, crcpp::Class& dest, FILE* fp)
	{
		Read(ctx, (crcpp::Type&)dest, fp);
		ReadPrimitivePtr(ctx, dest.base_class, fp);
		Read(ctx, dest.size, fp);
		ReadPrimitivePtrArray(ctx, dest.enums, fp);
		ReadPrimitivePtrArray(ctx, dest.classes, fp);
		ReadPrimitivePtrArray(ctx, dest.methods, fp);
		ReadPrimitivePtrArray(ctx, dest.fields, fp);
	}


	template <> void Read(const ReadContext& ctx, crcpp::Namespace& dest, FILE* fp)
	{
		Read(ctx, (crcpp::Primitive&)dest, fp);
		ReadPrimitivePtrArray(ctx, dest.namespaces, fp);
		ReadPrimitivePtrArray(ctx, dest.types, fp);
		ReadPrimitivePtrArray(ctx, dest.enums, fp);
		ReadPrimitivePtrArray(ctx, dest.classes, fp);
		ReadPrimitivePtrArray(ctx, dest.functions, fp);
	}
}


const crcpp::Primitive* crcpp::FindPrimitive(const ConstArray<const Primitive*>& primitives, Name name)
{
	int first = 0;
	int last = primitives.size() - 1;

	// Binary search
	while (first < last)
	{
		// Identify the mid point
		int mid = (first + last) / 2;
		unsigned int compare_hash = primitives[mid]->name.hash;

		if (name.hash > compare_hash)
		{
			// Shift search to local upper half
			first = mid + 1;
		}
		else if (name.hash < compare_hash)
		{
			// Shift search to local lower half
			last = mid - 1;
		}
		else
		{
			// Exact match found
			return primitives[mid];
		}
	}

	return 0;
}


crcpp::Database::Database()
	: m_NameTextData(0)
{
}


crcpp::Database::~Database()
{
	delete [] m_NameTextData;
}


bool crcpp::Database::Load(const char* filename)
{
	ReadContext ctx;

	// Try to open the file
	FILE* fp = fopen(filename, "rb");
	if (fp == 0)
	{
		return false;
	}

	// Do the signatures match?
	char signature[sizeof(DATABASE_SIGNATURE)];
	fread(signature, sizeof(signature), 1, fp);
	if (memcmp(signature, DATABASE_SIGNATURE, sizeof(signature)))
	{
		return false;
		fclose(fp);
	}

	// Do the versions match?
	unsigned int version = Read<unsigned int>(ctx, fp);
	if (version != DATABASE_VERSION)
	{
		return false;
		fclose(fp);
	}

	// Allocate an initially empty primitive array
	ctx.nb_primitives = Read<int>(ctx, fp);
	m_Primitives = ConstArray<const Primitive*>(ctx.nb_primitives);
	ctx.primitives = const_cast<const crcpp::Primitive**>(m_Primitives.data());

	// Read in the name text data
	ctx.name_data_size = Read<int>(ctx, fp);
	m_NameTextData = new const char[ctx.name_data_size];
	fread((void*)m_NameTextData, ctx.name_data_size, 1, fp);
	ctx.name_data = m_NameTextData;

	// Read the name table
	int nb_names = Read<int>(ctx, fp);
	m_Names = ConstArray<Name>(nb_names);
	for (int i = 0; i < nb_names; i++)
	{
		Name& name = const_cast<Name&>(m_Names[i]);
		Read(ctx, name, fp);
	}

	// Read and take ownership of the global namespace
	ReadArray(ctx, m_Types, fp);
	ReadArray(ctx, m_Enums, fp);
	ReadArray(ctx, m_Functions, fp);
	ReadArray(ctx, m_Classes, fp);
	ReadArray(ctx, m_Namespaces, fp);

	fclose(fp);
	return true;
}