
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clcpp/clcpp.h>


#if defined(CLCPP_PLATFORM_WINDOWS)
	// Windows-specific module loading and inspection functions
	typedef int (__stdcall *FunctionPtr)();
	extern "C" __declspec(dllimport) void* __stdcall LoadLibraryA(const char* lpLibFileName);
	extern "C" __declspec(dllimport) FunctionPtr __stdcall GetProcAddress(void* module, const char* lpProcName);
	extern "C" __declspec(dllimport) int __stdcall FreeLibrary(void* hLibModule);
	extern "C" __declspec(dllimport) void* __stdcall GetModuleHandleA(const char* lpModuleName);
#endif


#if defined(CLCPP_PLATFORM_POSIX)
	// We use POSIX-compatible dynamic linking loader interface, which
	// should be present on both Mac and Linux
	extern "C" int dlclose(void * __handle);
	extern "C" void * dlopen(const char * __path, int __mode);
	extern "C" void * dlsym(void * __handle, const char * __symbol);

	// TODO: check the loading flags when we can get this running, current
	// flag indicates RTLD_LAZY
	#define LOADING_FLAGS 0x1
#endif


namespace
{
	struct PtrSchema
	{
		clcpp::size_type stride;
		clcpp::size_type ptrs_offset;
		clcpp::size_type nb_ptrs;
	};

	
	struct PtrRelocation
	{
		int schema_handle;
		clcpp::size_type offset;
		int nb_objects;
	};


	// Rotate left - some compilers can optimise this to a single rotate!
	unsigned int rotl(unsigned int v, unsigned int bits)
	{
		return (v << bits) | (v >> (32 - bits));
	}


	unsigned int fmix(unsigned int h)
	{
		// Finalisation mix - force all bits of a hash block to avalanche
		h ^= h >> 16;
		h *= 0x85ebca6b;
		h ^= h >> 13;
		h *= 0xc2b2ae35;
		h ^= h >> 16;
		return h;
	}


	//
	// Austin Appleby's MurmurHash 3: http://code.google.com/p/smhasher
	//
	unsigned int MurmurHash3(const void* key, int len, unsigned int seed)
	{
		const unsigned char* data = (const unsigned char*)key;
		int nb_blocks = len / 4;

		unsigned int h1 = seed;
		unsigned int c1 = 0xcc9e2d51;
		unsigned int c2 = 0x1b873593;

		// Body
		const unsigned int* blocks = (const unsigned int*)(data + nb_blocks * 4);
		for (int i = -nb_blocks; i; i++)
		{
			unsigned int k1 = blocks[i];

			k1 *= c1;
			k1 = rotl(k1, 15);
			k1 *= c2;

			h1 ^= k1;
			h1 = rotl(h1, 13);
			h1 = h1 * 5 + 0xe6546b64;
		}

		// Tail
		const unsigned char* tail = (const unsigned char*)(data + nb_blocks * 4);
		unsigned int k1 = 0;
		switch (len & 3)
		{
		case (3): k1 ^= tail[2] << 16;
		case (2): k1 ^= tail[1] << 8;
		case (1): k1 ^= tail[0];
			k1 *= c1;
			k1 = rotl(k1, 15);
			k1 *= c2;
			h1 ^= k1;
		}

		// Finalisation
		h1 ^= len;
		h1 = fmix(h1);
		return h1;
	}


	int strlen(const char* str)
	{
		int len = 0;
		while (*str++)
			len++;
		return len;
	}


	unsigned int GetNameHash(clcpp::Name name)
	{
		return name.hash;
	}
	unsigned int GetPrimitiveHash(const clcpp::Primitive& primitive)
	{
		return primitive.name.hash;
	}
	unsigned int GetPrimitivePtrHash(const clcpp::Primitive* primitive)
	{
		return primitive->name.hash;
	}


	template <typename ARRAY_TYPE, typename COMPARE_L_TYPE, unsigned int (GET_HASH_FUNC)(COMPARE_L_TYPE)>
	int BinarySearch(const clcpp::CArray<ARRAY_TYPE>& entries, unsigned int compare_hash)
	{
		int first = 0;
		int last = entries.size() - 1;

		// Binary search
		while (first <= last)
		{
			// Identify the mid point
			int mid = (first + last) / 2;

			unsigned entry_hash = GET_HASH_FUNC(entries[mid]);
			if (compare_hash > entry_hash)
			{
				// Shift search to local upper half
				first = mid + 1;
			}
			else if (compare_hash < entry_hash)
			{
				// Shift search to local lower half
				last = mid - 1;
			}
			else
			{
				// Exact match found
				return mid;
			}
		}

		return -1;
	}


	template <typename ARRAY_TYPE, typename COMPARE_L_TYPE, unsigned int (GET_HASH_FUNC)(COMPARE_L_TYPE)>
	clcpp::Range SearchNeighbours(const clcpp::CArray<ARRAY_TYPE>& entries, unsigned int compare_hash, int index)
	{
		clcpp::Range range;
		range.first = index;
		range.last = index + 1;

		// Search either side of the result, gathering further matches
		while (range.first > 0 && GET_HASH_FUNC(entries[range.first - 1]) == compare_hash)
			range.first--;
		while (range.last < entries.size() && GET_HASH_FUNC(entries[range.last]) == compare_hash)
			range.last++;

		return range;
	}


	clcpp::internal::DatabaseMem* LoadMemoryMappedDatabase(clcpp::IFile* file, clcpp::IAllocator* allocator)
	{
		// Read the header and verify the version and signature
		clcpp::internal::DatabaseFileHeader file_header, cmp_header;
		if (!file->Read(file_header))
			return 0;
		if (file_header.version != cmp_header.version)
			return 0;
		if (file_header.signature0 != cmp_header.signature0 || file_header.signature1 != cmp_header.signature1)
			return 0;

		// Read the memory mapped data
		char* base_data = (char*)allocator->Alloc(file_header.data_size);
		clcpp::internal::DatabaseMem* database_mem = (clcpp::internal::DatabaseMem*)base_data;
		if (!file->Read(base_data, file_header.data_size))
			return 0;

		// Read the schema descriptions
		clcpp::CArray<PtrSchema> schemas(file_header.nb_ptr_schemas, allocator);
		if (!file->Read(schemas))
			return 0;

		// Read the pointer offsets for all the schemas
		clcpp::CArray<clcpp::size_type> ptr_offsets(file_header.nb_ptr_offsets, allocator);
		if (!file->Read(ptr_offsets))
			return 0;

		// Read the pointer relocation instructions
		clcpp::CArray<PtrRelocation> relocations(file_header.nb_ptr_relocations, allocator);
		if (!file->Read(relocations))
			return 0;

		// Iterate over every relocation instruction
		for (int i = 0; i < file_header.nb_ptr_relocations; i++)
		{
			PtrRelocation& reloc = relocations[i];
			PtrSchema& schema = schemas[reloc.schema_handle];

			// Take a weak C-array pointer to the schema's pointer offsets (for bounds checking)
			clcpp::CArray<clcpp::size_type> schema_ptr_offsets(&ptr_offsets[schema.ptrs_offset], schema.nb_ptrs);

			// Iterate over all objects in the instruction
			for (int j = 0; j < reloc.nb_objects; j++)
			{
	            clcpp::size_type object_offset = reloc.offset + j * schema.stride;

				// All pointers in the schema
				for (clcpp::size_type k = 0; k < schema.nb_ptrs; k++)
				{
	                clcpp::size_type ptr_offset = object_offset + schema_ptr_offsets[k];
	                clcpp::size_type& ptr = (clcpp::size_type&)*(base_data + ptr_offset);

					// Ensure the pointer relocation is within range of the memory map before patching
					clcpp::internal::Assert(ptr <= file_header.data_size);

					// Only patch non-null
					if (ptr != 0)
						ptr += (clcpp::size_type)base_data;
				}
			}
		}

		return database_mem;
	}


	void RebaseFunctions(clcpp::internal::DatabaseMem& dbmem, clcpp::pointer_type base_address)
	{
		// Move all function addresses from their current location to their new location
		for (int i = 0; i < dbmem.functions.size(); i++)
		{
			clcpp::Function& f = dbmem.functions[i];
			if (f.address)
				f.address = f.address - dbmem.function_base_address + base_address;
		}

		// Do the same for the GetType family of functions
		for (int i = 0; i < dbmem.get_type_functions.size(); i++)
		{
			clcpp::internal::GetTypeFunctions& f = dbmem.get_type_functions[i];
			if (f.get_typename_address)
				f.get_typename_address = f.get_typename_address - dbmem.function_base_address + base_address;
			if (f.get_type_address)
				f.get_type_address = f.get_type_address - dbmem.function_base_address + base_address;
		}
	}


	template<typename T>
	void PatchFunction(clcpp::pointer_type function_address,
	const T& patch_value, const T& original_value)
	{
		unsigned char* function_pointer = (unsigned char*) function_address;

		// searches for 0x20 instruction headers at most
		for (int i = 0; i < 0x20; i++)
		{
		#if defined(CLCPP_USING_64_BIT)
			// 64 bit patch starts here
			if (function_pointer[i] == 0x8B)
			{
				// MOV instruction
				// this may be what we are looking for
				clcpp::uint32 target_offset = *((clcpp::uint32*) &function_pointer[i + 2]);
				T* target_ptr = (T*) (&function_pointer[i + 6] + target_offset);

				// although we get the real target address here, we still cannot
				// say immediately if this is our target pointer to patch. On
				// Mac OS X, this pointer would first point to a stub, which
				// then points to the real pointer, so we may have two levels
				// of pointers here. In this case, we would test for both cases.
				if (*target_ptr == original_value)
				{
					// there's no stubs, this is the pointer we are looking for
					*target_ptr = patch_value;
					return;
				}

			#if defined(CLCPP_USING_GNUC_MAC)
				// we test for stub pointer on mac
				// TODO: check if other compiler has the same behaviour
				T* target_stub = *((T**) target_ptr);
				if (*target_stub == original_value)
				{
					*target_stub = patch_value;
					return;
				}
			#endif // CLCPP_USING_GNUC_MAC
			}
			// 64 bit patch ends here
		#else
			// 32 bit patch starts here
			if (function_pointer[i] == 0xA1)
			{
				T* target_addr = *((T**) (&function_pointer[i + 1]));

				if (*target_addr == original_value)
				{
					*target_addr = patch_value;
					return;
				}
			}
			// 32 bit patch ends here
		#endif // CLCPP_USING_64_BIT
		}

		// If this raises, the function has failed to patch
		// Load the database with OPT_DONT_PATCH_GETTYPE to prevent this
		clcpp::internal::Assert(false);
		return;
	}


	void PatchGetTypeAddresses(clcpp::Database& db, clcpp::internal::DatabaseMem& dbmem)
	{
		unsigned int original_hash = CLCPP_INVALID_HASH;
		const clcpp::Type* original_type = (const clcpp::Type*)CLCPP_INVALID_ADDRESS;

		for (int i = 0; i < dbmem.get_type_functions.size(); i++)
		{
			clcpp::internal::GetTypeFunctions& f = dbmem.get_type_functions[i];

			// Patch up the type name hash static variable
			if (f.get_typename_address)
			{
				unsigned int hash = db.GetName(f.type_hash).hash;
				PatchFunction<unsigned int>(f.get_typename_address,
					hash,
					original_hash);
			}

			// Patch up the type pointer static variable
			if (f.get_type_address)
			{
				const clcpp::Type* type = db.GetType(f.type_hash);
				PatchFunction<const clcpp::Type*>(f.get_type_address,
					type,
					original_type);
			}
		}
	}


	template <typename TYPE>
	void ParentPrimitivesToDatabase(clcpp::CArray<TYPE>& primitives, clcpp::Database* database)
	{
		for (int i = 0; i < primitives.size(); i++)
			primitives[i].database = database;
	}
}


void* clcpp::internal::LoadSharedLibrary(const char* filename)
{
#if defined(CLCPP_PLATFORM_WINDOWS)
	return LoadLibraryA(filename);
#elif defined(CLCPP_PLATFORM_POSIX)
	return dlopen(filename, LOADING_FLAGS);
#endif
}


void* clcpp::internal::GetSharedLibraryFunction(void* handle, const char* function_name)
{
#if defined(CLCPP_PLATFORM_WINDOWS)
	return GetProcAddress(handle, function_name);
#elif defined(CLCPP_PLATFORM_POSIX)
	return dlsym(handle, function_name);
#endif
}


void clcpp::internal::FreeSharedLibrary(void* handle)
{
#if defined(CLCPP_PLATFORM_WINDOWS)
	FreeLibrary(handle);
#elif defined(CLCPP_PLATFORM_POSIX)
	dlclose(handle);
#endif
}


clcpp::pointer_type clcpp::internal::GetLoadAddress()
{
#if defined(CLCPP_PLATFORM_WINDOWS)
	return (clcpp::pointer_type)GetModuleHandleA(0);
#elif defined(CLCPP_PLATFORM_POSIX)
	return (clcpp::pointer_type)dlopen(0, 0);
#endif
}


unsigned int clcpp::internal::HashData(const void* data, int length, unsigned int seed)
{
	return MurmurHash3(data, length, seed);
}


unsigned int clcpp::internal::HashNameString(const char* name_string, unsigned int seed)
{
	return MurmurHash3(name_string, strlen(name_string), seed);
}


unsigned int clcpp::internal::MixHashes(unsigned int a, unsigned int b)
{
	return MurmurHash3(&b, sizeof(unsigned int), a);
}


const clcpp::Primitive* clcpp::internal::FindPrimitive(const CArray<const Primitive*>& primitives, unsigned int hash)
{
	int index = BinarySearch<const Primitive*, const Primitive*, GetPrimitivePtrHash>(primitives, hash);
	if (index == -1)
		return 0;
	return primitives[index];
}


clcpp::Range clcpp::internal::FindOverloadedPrimitive(const CArray<const Primitive*>& primitives, unsigned int hash)
{
	// Search for the first entry
	int index = BinarySearch<const Primitive*, const Primitive*, GetPrimitivePtrHash>(primitives, hash);
	if (index == -1)
		return Range();

	// Look at its neighbours to widen the primitives found
	return SearchNeighbours<const Primitive*, const Primitive*, GetPrimitivePtrHash>(primitives, hash, index);
}


clcpp::Database::Database()
	: m_DatabaseMem(0)
	, m_Allocator(0)
{
}


clcpp::Database::~Database()
{
	if (m_DatabaseMem)
		m_Allocator->Free(m_DatabaseMem);
}


bool clcpp::Database::Load(IFile* file, IAllocator* allocator, unsigned int options)
{
	clcpp::pointer_type base_address = internal::GetLoadAddress();
	return Load(file, allocator, base_address, options);
}


bool clcpp::Database::Load(IFile* file, IAllocator* allocator, pointer_type base_address, unsigned int options)
{
	// Load the database
	internal::Assert(m_DatabaseMem == 0 && "Database already loaded");
	m_Allocator = allocator;
	m_DatabaseMem = LoadMemoryMappedDatabase(file, m_Allocator);

	if (m_DatabaseMem != 0)
	{
		// Rebasing functions is required mainly for DLLs and executables that run under Windows 7
		// using its Address Space Layout Randomisation security feature.
		if ((options & OPT_DONT_REBASE_FUNCTIONS) == 0)
			RebaseFunctions(*m_DatabaseMem, base_address);

		if ((options & OPT_DONT_PATCH_GETTYPE) == 0)
			PatchGetTypeAddresses(*this, *m_DatabaseMem);

		// Tell each loaded primitive that they belong to this database
		ParentPrimitivesToDatabase(m_DatabaseMem->types, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->enum_constants, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->enums, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->fields, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->functions, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->classes, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->templates, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->template_types, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->namespaces, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->flag_attributes, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->int_attributes, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->float_attributes, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->primitive_attributes, this);
		ParentPrimitivesToDatabase(m_DatabaseMem->text_attributes, this);
	}

	return m_DatabaseMem != 0;
}


clcpp::Name clcpp::Database::GetName(unsigned int hash) const
{
	// Lookup the name by hash
	int index = BinarySearch<Name, Name, GetNameHash>(m_DatabaseMem->names, hash);
	if (index == -1)
		return clcpp::Name();
	return m_DatabaseMem->names[index];
}


clcpp::Name clcpp::Database::GetName(const char* text) const
{
	// Null pointer
	if (text == 0)
		return clcpp::Name();

	// Hash and exit on no value
	unsigned int hash = internal::HashNameString(text);
	if (hash == 0)
		return clcpp::Name();

	return GetName(hash);
}


const clcpp::Type* clcpp::Database::GetType(unsigned int hash) const
{
	return FindPrimitive(m_DatabaseMem->type_primitives, hash);
}


const clcpp::Namespace* clcpp::Database::GetNamespace(unsigned int hash) const
{
	int index = BinarySearch<Namespace, const Primitive&, GetPrimitiveHash>(m_DatabaseMem->namespaces, hash);
	if (index == -1)
		return 0;
	return &m_DatabaseMem->namespaces[index];
}


const clcpp::Namespace* clcpp::Database::GetGlobalNamespace() const
{
	return &m_DatabaseMem->global_namespace;
}


const clcpp::Template* clcpp::Database::GetTemplate(unsigned int hash) const
{
	int index = BinarySearch<Template, const Primitive&, GetPrimitiveHash>(m_DatabaseMem->templates, hash);
	if (index == -1)
		return 0;
	return &m_DatabaseMem->templates[index];
}


const clcpp::Function* clcpp::Database::GetFunction(unsigned int hash) const
{
	int index = BinarySearch<Function, const Primitive&, GetPrimitiveHash>(m_DatabaseMem->functions, hash);
	if (index == -1)
		return 0;
	return &m_DatabaseMem->functions[index];
}


clcpp::Range clcpp::Database::GetOverloadedFunction(unsigned int hash) const
{
	// Quickly locate the first match
	int index = BinarySearch<Function, const Primitive&, GetPrimitiveHash>(m_DatabaseMem->functions, hash);
	if (index == -1)
		return Range();

	// Functions can be overloaded so look at the neighbours to widen the primitives found
	return SearchNeighbours<Function, const Primitive&, GetPrimitiveHash>(m_DatabaseMem->functions, hash, index);
}
