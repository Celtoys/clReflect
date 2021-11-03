
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clcpp/clcpp.h>
#include <clcpp/clcpp_internal.h>

#if defined(CLCPP_PLATFORM_WINDOWS)
extern "C" __declspec(dllimport) void* __stdcall GetModuleHandleA(const char* lpModuleName);
extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned int uExitCode);
#elif defined(CLCPP_PLATFORM_POSIX)
    #if defined(__linux__)
extern "C" void* dlopen(const char* __path, int __mode);
extern "C" void* dlsym(void* handle, const char* name);
    #elif defined(__APPLE__)
        // os x linker will bind it
        extern int start_base_address_hack __asm("section$start$__TEXT$__text");
    #endif
#endif

namespace
{
    struct PtrSchema
    {
        size_t stride;
        size_t ptrs_offset;
        size_t nb_ptrs;
    };

    struct PtrRelocation
    {
        int schema_handle;
        size_t offset;
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
        case (3):
            k1 ^= tail[2] << 16;
        case (2):
            k1 ^= tail[1] << 8;
        case (1):
            k1 ^= tail[0];
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

    template <typename ARRAY_TYPE, typename COMPARE_L_TYPE, unsigned int(GET_HASH_FUNC)(COMPARE_L_TYPE)>
    int BinarySearch(const clcpp::CArray<ARRAY_TYPE>& entries, unsigned int compare_hash)
    {
        int first = 0;
        int last = entries.size - 1;

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

    template <typename ARRAY_TYPE, typename COMPARE_L_TYPE, unsigned int(GET_HASH_FUNC)(COMPARE_L_TYPE)>
    clcpp::Range SearchNeighbours(const clcpp::CArray<ARRAY_TYPE>& entries, unsigned int compare_hash, int index)
    {
        clcpp::Range range;
        range.first = index;
        range.last = index + 1;

        // Search either side of the result, gathering further matches
        while (range.first > 0 && GET_HASH_FUNC(entries[range.first - 1]) == compare_hash)
            range.first--;
        while (range.last < entries.size && GET_HASH_FUNC(entries[range.last]) == compare_hash)
            range.last++;

        return range;
    }

    template <typename TYPE>
    bool ReadArray(clcpp::IFile* file, clcpp::CArray<TYPE>& array, unsigned int size, clcpp::IAllocator* allocator)
    {
        // Allocate space for the data
        array.size = size;
        array.data = (TYPE*)allocator->Alloc(size * sizeof(TYPE));

        // Read from the file, deleting on failure
        if (!file->Read((void*)array.data, array.size * sizeof(TYPE)))
        {
            allocator->Free(array.data);
            return false;
        }

        return true;
    }

    clcpp::internal::DatabaseMem* LoadMemoryMappedDatabase(clcpp::IFile* file, clcpp::IAllocator* allocator)
    {
        // Read the header and verify the version and signature
        clcpp::internal::DatabaseFileHeader file_header, cmp_header;
        if (!file->Read(&file_header, sizeof(file_header)))
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
        clcpp::CArray<PtrSchema> schemas;
        if (!ReadArray(file, schemas, file_header.nb_ptr_schemas, allocator))
            return 0;

        // Read the pointer offsets for all the schemas
        clcpp::CArray<size_t> ptr_offsets;
        if (!ReadArray(file, ptr_offsets, file_header.nb_ptr_offsets, allocator))
            return 0;

        // Read the pointer relocation instructions
        clcpp::CArray<PtrRelocation> relocations;
        if (!ReadArray(file, relocations, file_header.nb_ptr_relocations, allocator))
            return 0;

        // Iterate over every relocation instruction
        for (int i = 0; i < file_header.nb_ptr_relocations; i++)
        {
            PtrRelocation& reloc = (PtrRelocation&)relocations[i];
            PtrSchema& schema = (PtrSchema&)schemas[reloc.schema_handle];

            // Take a weak C-array pointer to the schema's pointer offsets (for bounds checking)
            clcpp::CArray<size_t> schema_ptr_offsets;
            schema_ptr_offsets.data = (size_t*)&ptr_offsets[schema.ptrs_offset];
            schema_ptr_offsets.size = (unsigned int)schema.nb_ptrs;

            // Iterate over all objects in the instruction
            for (int j = 0; j < reloc.nb_objects; j++)
            {
                size_t object_offset = reloc.offset + (size_t)j * schema.stride;

                // All pointers in the schema
                for (size_t k = 0; k < schema.nb_ptrs; k++)
                {
                    size_t ptr_offset = object_offset + schema_ptr_offsets[k];
                    size_t& ptr = (size_t&)*(base_data + ptr_offset);

                    // Ensure the pointer relocation is within range of the memory map before patching
                    clcpp::internal::Assert(ptr <= file_header.data_size);

                    // Only patch non-null
                    if (ptr != 0)
                        ptr += (clcpp::size_type)base_data;
                }
            }
        }

        // Release temporary array memory
        allocator->Free(relocations.data);
        allocator->Free(ptr_offsets.data);
        allocator->Free(schemas.data);

        return database_mem;
    }

    void RebaseFunctions(clcpp::internal::DatabaseMem& dbmem, clcpp::pointer_type base_address)
    {
        // Move all function addresses from their current location to their new location
        for (unsigned int i = 0; i < dbmem.functions.size; i++)
        {
            clcpp::Function& f = (clcpp::Function&)dbmem.functions[i];
            if (f.address)
                f.address = f.address - dbmem.function_base_address + base_address;
        }
    }

    template <typename TYPE>
    void ParentPrimitivesToDatabase(clcpp::CArray<TYPE>& primitives, clcpp::Database* database)
    {
        for (unsigned int i = 0; i < primitives.size; i++)
            ((clcpp::Primitive&)primitives[i]).database = database;
    }

    clcpp::pointer_type GetLoadAddress()
    {
    #if defined(CLCPP_PLATFORM_WINDOWS)
        return (clcpp::pointer_type)GetModuleHandleA(0);
    #elif defined(CLCPP_PLATFORM_POSIX)
        #if defined(__linux__)
        void* global_symbols = dlopen(0, 0);
        return (clcpp::pointer_type)dlsym(global_symbols, "_start");
        #elif defined(__APPLE__)
        return (clcpp::pointer_type)&start_base_address_hack;
        #endif
    #endif
    }
}

CLCPP_API void clcpp::internal::Assert(bool expression)
{
    if (expression == false)
    {
#if defined(CLCPP_USING_MSVC) && defined(_M_IX86)
        __asm
        {
            int 3h
        }
#else
        asm("int $0x3\n");
#endif // CLCPP_USING_MSVC

// Leave the program with no continuation
// Don't want people attaching the debugger and skipping over the break
#ifdef CLCPP_PLATFORM_WINDOWS
        ExitProcess(1);
#endif
    }
}

CLCPP_API unsigned int clcpp::internal::HashData(const void* data, int length, unsigned int seed)
{
    return MurmurHash3(data, length, seed);
}

CLCPP_API unsigned int clcpp::internal::HashNameString(const char* name_string, unsigned int seed)
{
    return MurmurHash3(name_string, strlen(name_string), seed);
}

CLCPP_API unsigned int clcpp::internal::MixHashes(unsigned int a, unsigned int b)
{
    return MurmurHash3(&b, sizeof(unsigned int), a);
}

clcpp::Range::Range()
    : first(0)
    , last(0)
{
}

CLCPP_API const clcpp::Primitive* clcpp::internal::FindPrimitive(const CArray<const Primitive*>& primitives, unsigned int hash)
{
    int index = BinarySearch<const Primitive*, const Primitive*, GetPrimitivePtrHash>(primitives, hash);
    if (index == -1)
        return 0;
    return primitives[index];
}

CLCPP_API clcpp::Range clcpp::internal::FindOverloadedPrimitive(const CArray<const Primitive*>& primitives, unsigned int hash)
{
    // Search for the first entry
    int index = BinarySearch<const Primitive*, const Primitive*, GetPrimitivePtrHash>(primitives, hash);
    if (index == -1)
        return Range();

    // Look at its neighbours to widen the primitives found
    return SearchNeighbours<const Primitive*, const Primitive*, GetPrimitivePtrHash>(primitives, hash, index);
}

clcpp::Name::Name()
    : hash(0)
    , text(0)
{
}

clcpp::Qualifier::Qualifier()
    : op(VALUE)
    , is_const(false)
{
}

clcpp::Qualifier::Qualifier(Operator op, bool is_const)
    : op(op)
    , is_const(is_const)
{
}

clcpp::ContainerInfo::ContainerInfo()
    : read_iterator_type(0)
    , write_iterator_type(0)
    , flags(0)
{
}

clcpp::Primitive::Primitive(Kind k)
    : kind(k)
    , parent(0)
    , database(0)
{
}

clcpp::Attribute::Attribute()
    : Primitive(KIND)
{
}

clcpp::Attribute::Attribute(Kind k)
    : Primitive(k)
{
}

const clcpp::IntAttribute* clcpp::Attribute::AsIntAttribute() const
{
    internal::Assert(kind == IntAttribute::KIND);
    return (const IntAttribute*)this;
}

const clcpp::FloatAttribute* clcpp::Attribute::AsFloatAttribute() const
{
    internal::Assert(kind == FloatAttribute::KIND);
    return (const FloatAttribute*)this;
}

const clcpp::PrimitiveAttribute* clcpp::Attribute::AsPrimitiveAttribute() const
{
    internal::Assert(kind == PrimitiveAttribute::KIND);
    return (const PrimitiveAttribute*)this;
}

const clcpp::TextAttribute* clcpp::Attribute::AsTextAttribute() const
{
    internal::Assert(kind == TextAttribute::KIND);
    return (const TextAttribute*)this;
}

clcpp::Type::Type()
    : Primitive(KIND)
    , size(0)
    , ci(0)
{
}

clcpp::Type::Type(Kind k)
    : Primitive(k)
    , size(0)
    , ci(0)
{
}

bool clcpp::Type::DerivesFrom(unsigned int type_name_hash) const
{
    // Search in immediate bases
    for (unsigned int i = 0; i < base_types.size; i++)
    {
        if (base_types[i]->name.hash == type_name_hash)
            return true;
    }

    // Search up the inheritance tree
    for (unsigned int i = 0; i < base_types.size; i++)
    {
        if (base_types[i]->DerivesFrom(type_name_hash))
            return true;
    }

    return false;
}

const clcpp::Enum* clcpp::Type::AsEnum() const
{
    internal::Assert(kind == Enum::KIND);
    return (const Enum*)this;
}

const clcpp::TemplateType* clcpp::Type::AsTemplateType() const
{
    internal::Assert(kind == TemplateType::KIND);
    return (const TemplateType*)this;
}

const clcpp::Class* clcpp::Type::AsClass() const
{
    internal::Assert(kind == Class::KIND);
    return (const Class*)this;
}

clcpp::EnumConstant::EnumConstant()
    : Primitive(KIND)
    , value(0)
{
}

clcpp::Enum::Enum()
    : Type(KIND)
    , flag_attributes(0)
{
}

const char* clcpp::Enum::GetValueName(int value) const
{
    // Linear search for a matching constant value
    for (unsigned int i = 0; i < constants.size; i++)
    {
        const clcpp::EnumConstant* constant = constants[i];
        if (constant->value == value)
            return constant->name.text;
    }

    return 0;
}

clcpp::Field::Field()
    : Primitive(KIND)
    , type(0)
    , offset(0)
    , parent_unique_id(0)
    , flag_attributes(0)
    , ci(0)
{
}

bool clcpp::Field::IsFunctionParameter() const
{
    return parent_unique_id != 0;
}

clcpp::Function::Function()
    : Primitive(KIND)
    , unique_id(0)
    , return_parameter(0)
    , flag_attributes(0)
{
}

clcpp::TemplateType::TemplateType()
    : Type(KIND)
    , constructor(0)
    , destructor(0)
{
    for (int i = 0; i < MAX_NB_ARGS; i++)
    {
        parameter_types[i] = 0;
        parameter_ptrs[i] = false;
    }
}

clcpp::Template::Template()
    : Primitive(KIND)
{
}

clcpp::Class::Class()
    : Type(KIND)
    , constructor(0)
    , destructor(0)
    , flag_attributes(0)
{
}

clcpp::Namespace::Namespace()
    : Primitive(KIND)
{
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
    clcpp::pointer_type base_address = GetLoadAddress();
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

const clcpp::Type** clcpp::Database::GetTypes(unsigned int& out_nb_types) const
{
    out_nb_types = m_DatabaseMem->type_primitives.size;
    return m_DatabaseMem->type_primitives.data;
}

void clcpp::Database::SetTypes(const clcpp::Type** types, unsigned int nb_types)
{
    m_DatabaseMem->type_primitives.data = types;
    m_DatabaseMem->type_primitives.size = nb_types;
}

const clcpp::Function* clcpp::Database::GetFunctions(unsigned int& out_nb_functions) const
{
    out_nb_functions = m_DatabaseMem->functions.size;
    return m_DatabaseMem->functions.data;
}

clcpp::internal::DatabaseMem::DatabaseMem()
    : function_base_address(0)
    , name_text_data(0)
{
}

clcpp::internal::DatabaseFileHeader::DatabaseFileHeader()
    : signature0('pclc')
    , signature1('\0bdp')
    , version(2)
    , nb_ptr_schemas(0)
    , nb_ptr_offsets(0)
    , nb_ptr_relocations(0)
    , data_size(0)
{
}
