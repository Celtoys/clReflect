
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//
// TODO:
//    * Allow names to be specified as CRCs?
//    * Escape sequences need converting.
//    * Enums communicated by value (load integer checks for enum - could have a verify mode to ensure the constant exists).
//    * Field names need to be in memory for JSON serialising to work.
//

#include <clcpp/Containers.h>
#include <clcpp/FunctionCall.h>
#include <clutl/JSONLexer.h>
#include <clutl/Serialise.h>

// Explicitly stated dependencies in stdlib.h
// Non-standard, writes at most n bytes to dest with printf formatting
#if defined(CLCPP_PLATFORM_WINDOWS)
extern "C" int _snprintf(char* dest, unsigned int n, const char* fmt, ...);
    #define snprintf _snprintf
#else
extern "C" int snprintf(char* dest, unsigned int n, const char* fmt, ...);
#endif

static void SetupTypeDispatchLUT();

namespace
{
    // ----------------------------------------------------------------------------------------------------
    // Perfect hash based load/save function dispatching
    // ----------------------------------------------------------------------------------------------------

    using SaveNumberFunc = void (*)(clutl::WriteBuffer&, const char*, unsigned int);
    using LoadIntegerFunc = void (*)(char*, clcpp::int64);
    using LoadDecimalFunc = void (*)(char*, double);

    struct TypeDispatch
    {
        bool valid = false;
        SaveNumberFunc saveNumber = nullptr;
        LoadIntegerFunc loadInteger = nullptr;
        LoadDecimalFunc loadDecimal = nullptr;
    };

    // A save function lookup table for all supported C++ types
    const int g_TypeDispatchMod = 47;
    TypeDispatch g_TypeDispatchLUT[g_TypeDispatchMod];
    bool g_TypeDispatchLUTReady = false;

    // For the given data set of basic type name hashes, this combines to make
    // a perfect hash function with no collisions, allowing quick indexed lookup.
    unsigned int GetTypeDispatchIndex(unsigned int hash)
    {
        return hash % g_TypeDispatchMod;
    }
    unsigned int GetTypeDispatchIndex(const char* type_name)
    {
        return GetTypeDispatchIndex(clcpp::internal::HashNameString(type_name));
    }

    void AddTypeDispatch(const char* type_name, SaveNumberFunc save_func, LoadIntegerFunc loadi_func, LoadDecimalFunc loadd_func)
    {
        // Ensure there are no collisions before adding the functions
        unsigned index = GetTypeDispatchIndex(type_name);
        clcpp::internal::Assert(g_TypeDispatchLUT[index].valid == false && "Lookup table index collision");
        g_TypeDispatchLUT[index].valid = true;
        g_TypeDispatchLUT[index].saveNumber = save_func;
        g_TypeDispatchLUT[index].loadInteger = loadi_func;
        g_TypeDispatchLUT[index].loadDecimal = loadd_func;
    }

    // ----------------------------------------------------------------------------------------------------
    // JSON Parser & reflection-based object construction
    // ----------------------------------------------------------------------------------------------------

    void ParserValue(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type,
                     clcpp::Qualifier::Operator op, const clcpp::Field* field, unsigned int transient_flags);
    void ParserObject(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type,
                      unsigned int transient_flags);

    clutl::JSONToken Expect(clutl::JSONContext& ctx, clutl::JSONToken& t, clutl::JSONTokenType type)
    {
        // Check the tokens match
        if (t.type != type)
        {
            ctx.SetError(clutl::JSONError::UNEXPECTED_TOKEN);
            return clutl::JSONToken();
        }

        // Look-ahead one token
        clutl::JSONToken old = t;
        t = LexerNextToken(ctx);
        return old;
    }

    void ParserString(const clutl::JSONToken& t, char* object, const clcpp::Type* type)
    {
        // Was there an error expecting a string?
        if (!t.IsValid())
        {
            return;
        }

        // With enum fields use name lookup to match constants
        if (type != nullptr && type->kind == clcpp::Primitive::KIND_ENUM)
        {
            const clcpp::Enum* enum_type = type->AsEnum();

            // Is the enum a series of flags?
            static unsigned int hash = clcpp::internal::HashNameString("flags");
            bool are_flags = clcpp::FindPrimitive(enum_type->attributes, hash) != nullptr;
            if (are_flags)
            {
                int pos = 0;
                bool found_flags = false;
                while (pos < t.length)
                {
                    // Seek to end of the symbol
                    int start_pos = pos;
                    while (pos < t.length && t.val.string[pos] != '|')
                    {
                        pos++;
                    }

                    // OR in flag if it can be found
                    unsigned int constant_hash = clcpp::internal::HashData(t.val.string + start_pos, pos - start_pos);
                    const clcpp::EnumConstant* constant = clcpp::FindPrimitive(enum_type->constants, constant_hash);
                    if (constant != nullptr)
                    {
                        if (found_flags)
                        {
                            *reinterpret_cast<int*>(object) |= constant->value;
                        }
                        else
                        {
                            *reinterpret_cast<int*>(object) = constant->value;
                        }

                        // Only initialise to enum values of some are found
                        found_flags = true;
                    }

                    // Skip over | or beyond end-of-string
                    pos++;
                }
            }
            else
            {
                unsigned int constant_hash = clcpp::internal::HashData(t.val.string, t.length);
                const clcpp::EnumConstant* constant = clcpp::FindPrimitive(enum_type->constants, constant_hash);
                if (constant != nullptr)
                {
                    *reinterpret_cast<int*>(object) = constant->value;
                }
            }
        }
    }

    template <typename TYPE>
    void LoadIntegerWithCast(char* dest, clcpp::int64 integer)
    {
        *reinterpret_cast<TYPE*>(dest) = TYPE(integer);
    }
    void LoadIntegerBool(char* dest, clcpp::int64 integer)
    {
        *reinterpret_cast<bool*>(dest) = integer != 0;
    }

    template <typename TYPE>
    void LoadDecimalWithCast(char* dest, double decimal)
    {
        *reinterpret_cast<TYPE*>(dest) = TYPE(decimal);
    }
    void LoadDecimalBool(char* dest, double decimal)
    {
        *reinterpret_cast<bool*>(dest) = decimal != 0;
    }

    void LoadInteger(clcpp::int64 integer, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op)
    {
        if (type == nullptr)
        {
            return;
        }

        if (op == clcpp::Qualifier::POINTER)
        {
            *reinterpret_cast<unsigned int*>(object) = static_cast<unsigned int>(integer);
        }

        else
        {
            // Dispatch to the correct integer loader based on the field type
            unsigned int index = GetTypeDispatchIndex(type->name.hash);
            clcpp::internal::Assert(index < g_TypeDispatchMod && "Index is out of range");
            LoadIntegerFunc func = g_TypeDispatchLUT[index].loadInteger;
            if (func != nullptr)
            {
                func(object, integer);
            }
        }
    }

    void ParserInteger(const clutl::JSONToken& t, char* object, const clcpp::Type* type, clcpp::Qualifier::Operator op)
    {
        if (t.IsValid())
        {
            LoadInteger(t.val.integer, object, type, op);
        }
    }

    void ParserDecimal(const clutl::JSONToken& t, char* object, const clcpp::Type* type)
    {
        // Was there an error expecting a decimal?
        if (!t.IsValid())
        {
            return;
        }

        if (type != nullptr)
        {
            // Dispatch to the correct decimal loader based on the field type
            unsigned int index = GetTypeDispatchIndex(type->name.hash);
            clcpp::internal::Assert(index < g_TypeDispatchMod && "Index is out of range");
            LoadDecimalFunc func = g_TypeDispatchLUT[index].loadDecimal;
            if (func != nullptr)
            {
                func(object, t.val.decimal);
            }
        }
    }

    int ParserElements(clutl::JSONContext& ctx, clutl::JSONToken& t, clcpp::WriteIterator* writer, const clcpp::Type* type,
                       clcpp::Qualifier::Operator op, unsigned int transient_flags)
    {
        int count = 0;

        while (true)
        {
            count++;

            // Expect a value first
            if (writer != nullptr)
            {
                ParserValue(ctx, t, static_cast<char*>(writer->AddEmpty()), type, op, nullptr, transient_flags);
            }
            else
            {
                ParserValue(ctx, t, nullptr, nullptr, op, nullptr, transient_flags);
            }

            if (t.type != clutl::JSON_TOKEN_COMMA)
            {
                break;
            }

            t = LexerNextToken(ctx);
        }

        return count;
    }

    void ParserArray(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type,
                     const clcpp::Field* field, unsigned int transient_flags)
    {
        if (!Expect(ctx, t, clutl::JSON_TOKEN_LBRACKET).IsValid())
        {
            return;
        }

        // Empty array?
        if (t.type == clutl::JSON_TOKEN_RBRACKET)
        {
            t = LexerNextToken(ctx);
            return;
        }

        // TODO: Save a field specifying container size. This makes hand-authored JSON a little
        //       trickier but massively speeds up the general case of game read/written JSON.
        //       Could make a fallback case for when no container size is specified.

        clcpp::WriteIterator writer;
        if (field != nullptr && field->ci != nullptr)
        {
            // Fields are fixed array iterators
            writer.Initialise(field, object);
        }

        else if (type != nullptr && type->ci != nullptr)
        {
            // Do a pre-pass on the array to count the number of elements
            // Really not very efficient for big collections of large objects
            ctx.PushState(t);
            int array_count = ParserElements(ctx, t, nullptr, nullptr, clcpp::Qualifier::VALUE, transient_flags);
            ctx.PopState(t);

            // Template types are dynamic container iterators
            writer.Initialise(type->AsTemplateType(), object, array_count);
        }

        if (writer.IsInitialised())
        {
            ParserElements(ctx, t, &writer, writer.m_ValueType,
                           writer.m_ValueIsPtr ? clcpp::Qualifier::POINTER : clcpp::Qualifier::VALUE, transient_flags);
        }
        else
        {
            ParserElements(ctx, t, nullptr, nullptr, clcpp::Qualifier::VALUE, transient_flags);
        }

        Expect(ctx, t, clutl::JSON_TOKEN_RBRACKET);
    }

    void ParserLiteralValue(const clutl::JSONToken& t, int integer, char* object, const clcpp::Type* type,
                            clcpp::Qualifier::Operator op)
    {
        if (t.IsValid())
        {
            LoadInteger(integer, object, type, op);
        }
    }

    void ParserValue(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type,
                     clcpp::Qualifier::Operator op, const clcpp::Field* field, unsigned int transient_flags)
    {
        bool transient = false;
        if (type != nullptr && type->kind == clcpp::Primitive::KIND_CLASS)
        {
            const clcpp::Class* class_type = type->AsClass();

            // Does this class have a custom load function?
            if ((class_type->flag_attributes & attrFlag_CustomLoad) != 0)
            {
                // Look it up
                static unsigned int hash = clcpp::internal::HashNameString("load_json");
                if (const clcpp::Attribute* attr = clcpp::FindPrimitive(class_type->attributes, hash))
                {
                    const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();

                    // Call it and return immediately
                    clcpp::CallFunction(static_cast<const clcpp::Function*>(name_attr->primitive), clcpp::ByRef(t), object);
                    t = LexerNextToken(ctx);
                    return;
                }
            }

            // Record whether the value needs skipping because the class is transient
            transient = (class_type->flag_attributes & transient_flags) != 0;
        }

        switch (t.type)
        {
        case clutl::JSON_TOKEN_STRING:
            return ParserString(Expect(ctx, t, clutl::JSON_TOKEN_STRING), object, type);
        case clutl::JSON_TOKEN_INTEGER:
            return ParserInteger(Expect(ctx, t, clutl::JSON_TOKEN_INTEGER), object, type, op);
        case clutl::JSON_TOKEN_DECIMAL:
            return ParserDecimal(Expect(ctx, t, clutl::JSON_TOKEN_DECIMAL), object, type);
        case clutl::JSON_TOKEN_LBRACE: {
            if (type != nullptr && !transient)
            {
                ParserObject(ctx, t, object, type, transient_flags);
            }
            else
            {
                ParserObject(ctx, t, nullptr, nullptr, transient_flags);
            }

            Expect(ctx, t, clutl::JSON_TOKEN_RBRACE);
            break;
        }
        case clutl::JSON_TOKEN_LBRACKET:
            return ParserArray(ctx, t, object, type, field, transient_flags);
        case clutl::JSON_TOKEN_TRUE:
            return ParserLiteralValue(Expect(ctx, t, clutl::JSON_TOKEN_TRUE), 1, object, type, op);
        case clutl::JSON_TOKEN_FALSE:
            return ParserLiteralValue(Expect(ctx, t, clutl::JSON_TOKEN_FALSE), 0, object, type, op);
        case clutl::JSON_TOKEN_NULL:
            return ParserLiteralValue(Expect(ctx, t, clutl::JSON_TOKEN_NULL), 0, object, type, op);

        default:
            ctx.SetError(clutl::JSONError::UNEXPECTED_TOKEN);
            break;
        }
    }

    const clcpp::Field* FindFieldsRecursive(const clcpp::Type* type, unsigned int hash)
    {
        // Check fields if this is a class
        const clcpp::Field* field = nullptr;
        if (type->kind == clcpp::Primitive::KIND_CLASS)
        {
            field = clcpp::FindPrimitive(type->AsClass()->fields, hash);
        }

        if (field == nullptr)
        {
            // Search up through the inheritance hierarchy
            for (unsigned int i = 0; i < type->base_types.size; i++)
            {
                field = FindFieldsRecursive(type->base_types[i], hash);
                if (field != nullptr)
                {
                    break;
                }
            }
        }

        return field;
    }

    void ParserPair(clutl::JSONContext& ctx, clutl::JSONToken& t, char*& object, const clcpp::Type*& type,
                    unsigned int transient_flags)
    {
        // Get the field name
        clutl::JSONToken name = Expect(ctx, t, clutl::JSON_TOKEN_STRING);
        if (!name.IsValid())
        {
            return;
        }

        // Lookup the field in the parent class, if the type is class
        // We want to continue parsing even if there's a mismatch, to skip the invalid data
        const clcpp::Field* field = nullptr;
        if (type != nullptr && type->kind == clcpp::Primitive::KIND_CLASS)
        {
            const clcpp::Class* class_type = type->AsClass();
            unsigned int field_hash = clcpp::internal::HashData(name.val.string, name.length);

            field = FindFieldsRecursive(class_type, field_hash);

            // Don't load values for transient fields
            if (field != nullptr && (field->flag_attributes & transient_flags) != 0)
            {
                field = nullptr;
            }
        }

        if (!Expect(ctx, t, clutl::JSON_TOKEN_COLON).IsValid())
        {
            return;
        }

        // Parse or skip the field if it's unknown
        if (field != nullptr)
        {
            ParserValue(ctx, t, object + field->offset, field->type, field->qualifier.op, field, transient_flags);
        }
        else
        {
            ParserValue(ctx, t, nullptr, nullptr, clcpp::Qualifier::VALUE, nullptr, transient_flags);
        }
    }

    void ParserMembers(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type,
                       unsigned int transient_flags)
    {
        ParserPair(ctx, t, object, type, transient_flags);

        // Recurse, parsing more members in the list
        if (t.type == clutl::JSON_TOKEN_COMMA)
        {
            t = LexerNextToken(ctx);
            ParserMembers(ctx, t, object, type, transient_flags);
        }
    }

    void ParserDictionary(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type,
                          unsigned int transient_flags)
    {
        // Create the writer with an unknown count
        clcpp::WriteIterator writer;
        writer.Initialise(type, object, 0);

        // Loop until closing right brace found
        while (t.IsValid() && t.type != clutl::JSON_TOKEN_RBRACE)
        {
            // Parse the key, storing the value on the stack
            char key_data[128];
            clcpp::internal::Assert(writer.m_KeyType->size < sizeof(key_data));
            ParserValue(ctx, t, key_data, writer.m_KeyType, clcpp::Qualifier::VALUE, nullptr, transient_flags);

            // Key/value separator
            if (!Expect(ctx, t, clutl::JSON_TOKEN_COLON).IsValid())
            {
                return;
            }

            // Allocate space for new data and parse it
            void* value_data = writer.AddEmpty(key_data);
            ParserObject(ctx, t, static_cast<char*>(value_data), writer.m_ValueType, transient_flags);
            Expect(ctx, t, clutl::JSON_TOKEN_RBRACE);

            if (t.type == clutl::JSON_TOKEN_COMMA)
            {
                t = LexerNextToken(ctx);
            }
        }
    }

    void ParserObject(clutl::JSONContext& ctx, clutl::JSONToken& t, char* object, const clcpp::Type* type,
                      unsigned int transient_flags)
    {
        if (!Expect(ctx, t, clutl::JSON_TOKEN_LBRACE).IsValid())
        {
            return;
        }

        // Empty object?
        if (t.type == clutl::JSON_TOKEN_RBRACE)
        {
            // t = LexerNextToken(ctx);
            return;
        }

        // If we're parsing an object and the target type has a container info, this is a dictionary
        if (type != nullptr && type->ci != nullptr)
        {
            ParserDictionary(ctx, t, object, type, transient_flags);
        }
        else
        {
            ParserMembers(ctx, t, object, type, transient_flags);
        }

        if (type != nullptr && type->kind == clcpp::Primitive::KIND_CLASS)
        {
            const clcpp::Class* class_type = type->AsClass();

            // Run any attached post-load functions
            if ((class_type->flag_attributes & attrFlag_PostLoad) != 0)
            {
                static unsigned int hash = clcpp::internal::HashNameString("post_load");
                if (const clcpp::Attribute* attr = clcpp::FindPrimitive(class_type->attributes, hash))
                {
                    const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();
                    if (name_attr->primitive != nullptr)
                    {
                        clcpp::CallFunction(static_cast<const clcpp::Function*>(name_attr->primitive), object);
                    }
                }
            }
        }
    }
}

CLCPP_API clutl::JSONError clutl::LoadJSON(ReadBuffer& in, void* object, const clcpp::Type* type, unsigned int transient_flags)
{
    SetupTypeDispatchLUT();
    clutl::JSONContext ctx(in);
    clutl::JSONToken t = LexerNextToken(ctx);
    ParserObject(ctx, t, static_cast<char*>(object), type, transient_flags);
    return ctx.GetError();
}

CLCPP_API clutl::JSONError clutl::LoadJSON(clutl::JSONContext& ctx, void* object, const clcpp::Field* field,
                                           unsigned int transient_flags)
{
    SetupTypeDispatchLUT();
    clutl::JSONToken t = LexerNextToken(ctx);
    ParserValue(ctx, t, static_cast<char*>(object), field->type, field->qualifier.op, field, transient_flags);
    return ctx.GetError();
}

namespace
{
    // ----------------------------------------------------------------------------------------------------
    // JSON text writer using reflected objects
    // ----------------------------------------------------------------------------------------------------

    void SaveObject(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, const clcpp::Type* type,
                    clutl::IPtrMap* ptr_map, unsigned int flags, unsigned int transient_flags);

    void SaveString(clutl::WriteBuffer& out, const char* start, const char* end)
    {
        out.WriteChar('\"');
        out.Write(start, end - start);
        out.WriteChar('\"');
    }

    void SaveString(clutl::WriteBuffer& out, const char* str)
    {
        out.WriteChar('\"');
        out.WriteStr(str);
        out.WriteChar('\"');
    }

    void SaveInteger(clutl::WriteBuffer& out, clcpp::int64 integer)
    {
        // Enough to store a 64-bit int
        static const int MAX_SZ = 20;
        char text[MAX_SZ];

        // Null terminate and start at the end
        text[MAX_SZ - 1] = 0;
        char* end = text + MAX_SZ - 1;
        char* tptr = end;

        // Get the absolute and record if the value is negative
        bool negative = false;
        if (integer < 0)
        {
            negative = true;
            integer = -integer;
        }

        // Loop through the value with radix 10
        do
        {
            clcpp::int64 next_integer = integer / 10;
            *--tptr = char('0' + (integer - next_integer * 10));
            integer = next_integer;
        } while (integer != 0);

        // Add negative prefix
        if (negative)
        {
            *--tptr = '-';
        }

        out.Write(tptr, end - tptr);
    }

    void SaveUnsignedInteger(clutl::WriteBuffer& out, clcpp::uint64 integer)
    {
        // Enough to store a 64-bit int + null
        static const int MAX_SZ = 21;
        char text[MAX_SZ];

        // Null terminate and start at the end
        text[MAX_SZ - 1] = 0;
        char* end = text + MAX_SZ - 1;
        char* tptr = end;

        // Loop through the value with radix 10
        do
        {
            clcpp::uint64 next_integer = integer / 10;
            *--tptr = char('0' + (integer - next_integer * 10));
            integer = next_integer;
        } while (integer != 0);

        out.Write(tptr, end - tptr);
    }

    void SaveHexInteger(clutl::WriteBuffer& out, clcpp::uint64 integer)
    {
        // Enough to store a 64-bit int + null
        static const int MAX_SZ = 21;
        char text[MAX_SZ];

        // Null terminate and start at the end
        text[MAX_SZ - 1] = 0;
        char* end = text + MAX_SZ - 1;
        char* tptr = end;

        // Loop through the value with radix 16
        do
        {
            clcpp::uint64 next_integer = integer / 16;
            *--tptr = "0123456789ABCDEF"[integer - next_integer * 16];
            integer = next_integer;
        } while (integer != 0);

        out.Write(tptr, end - tptr);
    }

    // Automate the process of de-referencing an object as its exact type so no
    // extra bytes are read incorrectly and values are correctly zero-extended.
    template <typename TYPE>
    void SaveIntegerWithCast(clutl::WriteBuffer& out, const char* object, unsigned int)
    {
        SaveInteger(out, *reinterpret_cast<const TYPE*>(object));
    }
    template <typename TYPE>
    void SaveUnsignedIntegerWithCast(clutl::WriteBuffer& out, const char* object, unsigned int)
    {
        SaveUnsignedInteger(out, *reinterpret_cast<const TYPE*>(object));
    }

    void SaveDecimal(clutl::WriteBuffer& out, double decimal, unsigned int flags)
    {
        if ((flags & clutl::JSONFlags::EMIT_HEX_FLOATS) != 0)
        {
            // Use a specific prefix to inform the lexer to alias as a decimal
            out.WriteStr("0d");
            SaveHexInteger(out, reinterpret_cast<clcpp::uint64&>(decimal));
            return;
        }

        // Serialise full float rep
        char float_buffer[512];
        int count = snprintf(float_buffer, sizeof(float_buffer), "%f", decimal);
        out.Write(float_buffer, count);
    }

    void SaveDouble(clutl::WriteBuffer& out, const char* object, unsigned int flags)
    {
        SaveDecimal(out, *reinterpret_cast<const double*>(object), flags);
    }
    void SaveFloat(clutl::WriteBuffer& out, const char* object, unsigned int flags)
    {
        SaveDecimal(out, *reinterpret_cast<const float*>(object), flags);
    }

    void SaveType(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type, unsigned int flags)
    {
        unsigned int index = GetTypeDispatchIndex(type->name.hash);
        clcpp::internal::Assert(index < g_TypeDispatchMod && "Index is out of range");
        SaveNumberFunc func = g_TypeDispatchLUT[index].saveNumber;
        clcpp::internal::Assert(func != nullptr && "No save function for type");
        func(out, object, flags);
    }

    void SaveEnum(clutl::WriteBuffer& out, const char* object, const clcpp::Enum* enum_type)
    {
        int value = *reinterpret_cast<const int*>(object);

        // Is the enum a series of flags?
        static unsigned int hash = clcpp::internal::HashNameString("flags");
        bool are_flags = clcpp::FindPrimitive(enum_type->attributes, hash) != nullptr;
        if (are_flags && value != 0)
        {
            // Linear search of all enum values testing to see if they're set as flags
            bool enum_written = false;
            for (unsigned int i = 0; i < enum_type->constants.size; i++)
            {
                int enum_value = enum_type->constants[i]->value;
                if ((value & enum_value) != 0)
                {
                    // Save as a series of OR operations
                    if (enum_written)
                    {
                        out.WriteChar('|');
                    }
                    else
                    {
                        out.WriteChar('\"');
                    }
                    out.WriteStr(enum_type->constants[i]->name.text);

                    // Clear out flag and keep going if it's not finished
                    value &= ~enum_value;
                    enum_written = true;
                    if (value == 0)
                    {
                        break;
                    }
                }
            }

            if (enum_written)
            {
                out.WriteChar('\"');
            }
            else
            {
                SaveString(out, "clReflect_JSON_EnumFlagNotFound");
            }
        }
        else
        {
            // Do a linear search for an enum with a matching value
            // Also comes through here looking for match when value=0
            const char* enum_name = "clReflect_JSON_EnumValueNotFound";
            for (unsigned int i = 0; i < enum_type->constants.size; i++)
            {
                int enum_value = enum_type->constants[i]->value;
                if (enum_value == value)
                {
                    enum_name = enum_type->constants[i]->name.text;
                    break;
                }
            }

            // Write the enum name as the value
            SaveString(out, enum_name);
        }
    }

    void SavePtr(clutl::WriteBuffer& out, const void* object, clutl::IPtrMap* ptr_map, unsigned int flags)
    {
        // Get the filtered pointer from the caller
        void* ptr = *static_cast<void* const*>(object);
        unsigned int hash = ptr_map->MapPtr(ptr);

        if ((flags & clutl::JSONFlags::EMIT_HEX_POINTERS) != 0)
        {
            out.WriteStr("0x");
            SaveHexInteger(out, hash);
        }
        else
        {
            SaveUnsignedInteger(out, hash);
        }
    }

    void SaveContainer(clutl::WriteBuffer& out, clcpp::ReadIterator& reader, const clcpp::Field* field, clutl::IPtrMap* ptr_map,
                       unsigned int flags, unsigned int transient_flags)
    {
        // TODO: The reader knows its type and if its a pointer for all entries. Can early out on unwanted pointer saves, etc.

        out.WriteChar(reader.m_KeyType != nullptr ? '{' : '[');

        // Save comma-separated objects
        bool written = false;
        for (unsigned int i = 0; i < reader.m_Count; i++)
        {
            clcpp::ContainerKeyValue kv = reader.GetKeyValue();

            if (written)
            {
                out.WriteChar(',');
            }

            // Write the key value
            if (reader.m_KeyType != nullptr)
            {
                // TODO(don): Support for pointer keys.
                SaveObject(out, static_cast<const char*>(kv.key), field, reader.m_KeyType, ptr_map, flags, transient_flags);
                out.WriteChar(':');
            }

            if (reader.m_ValueIsPtr)
            {
                // Ask the user if they want to save this pointer
                void* ptr = *static_cast<void* const*>(kv.value);
                if (ptr_map == nullptr || !ptr_map->CanMapPtr(ptr, reader.m_ValueType))
                {
                    continue;
                }

                SavePtr(out, kv.value, ptr_map, flags);
            }
            else
            {
                SaveObject(out, static_cast<const char*>(kv.value), field, reader.m_ValueType, ptr_map, flags, transient_flags);
            }

            reader.MoveNext();
            written = true;
        }

        out.WriteChar(reader.m_KeyType != nullptr ? '}' : ']');
    }

    void SaveFieldArray(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, clutl::IPtrMap* ptr_map,
                        unsigned int flags, unsigned int transient_flags)
    {
        // Construct a read iterator and leave early if there are no elements
        clcpp::ReadIterator reader;
        reader.Initialise(field, object);
        if (reader.m_Count == 0)
        {
            out.WriteStr("[]");
            return;
        }

        SaveContainer(out, reader, field, ptr_map, flags, transient_flags);
    }

    inline void NewLine(clutl::WriteBuffer& out, unsigned int flags)
    {
        if ((flags & clutl::JSONFlags::FORMAT_OUTPUT) != 0)
        {
            out.WriteChar('\n');

            // Open the next new line with tabs
            for (unsigned int i = 0; i < (flags & clutl::JSONFlags::INDENT_MASK); i++)
            {
                out.WriteChar('\t');
            }
        }
    }

    inline void OpenScope(clutl::WriteBuffer& out, unsigned int& flags)
    {
        if ((flags & clutl::JSONFlags::FORMAT_OUTPUT) != 0)
        {
            NewLine(out, flags);
            out.WriteChar('{');

            // Increment indent level
            int indent_level = flags & clutl::JSONFlags::INDENT_MASK;
            flags &= ~clutl::JSONFlags::INDENT_MASK;
            flags |= ((indent_level + 1) & clutl::JSONFlags::INDENT_MASK);

            NewLine(out, flags);
        }
        else
        {
            out.WriteChar('{');
        }
    }

    inline void CloseScope(clutl::WriteBuffer& out, unsigned int& flags)
    {
        if ((flags & clutl::JSONFlags::FORMAT_OUTPUT) != 0)
        {
            // Decrement indent level
            int indent_level = flags & clutl::JSONFlags::INDENT_MASK;
            flags &= ~clutl::JSONFlags::INDENT_MASK;
            flags |= ((indent_level - 1) & clutl::JSONFlags::INDENT_MASK);

            NewLine(out, flags);
            out.WriteChar('}');
            NewLine(out, flags);
        }
        else
        {
            out.WriteChar('}');
        }
    }

    void SaveFieldObject(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, clutl::IPtrMap* ptr_map,
                         unsigned int& flags, unsigned int transient_flags)
    {
        if (field->ci != nullptr)
        {
            SaveFieldArray(out, object, field, ptr_map, flags, transient_flags);
        }
        else if (field->qualifier.op == clcpp::Qualifier::POINTER)
        {
            SavePtr(out, object, ptr_map, flags);
        }
        else
        {
            SaveObject(out, object, field, field->type, ptr_map, flags, transient_flags);
        }
    }

    void SaveClassField(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, clutl::IPtrMap* ptr_map,
                        unsigned int& flags, bool& field_written, unsigned int transient_flags)
    {
        if (field->qualifier.op == clcpp::Qualifier::POINTER)
        {
            // Ask the caller if they want to save this pointer
            const void* ptr = *reinterpret_cast<void* const*>(object + field->offset);
            if (ptr_map == nullptr || !ptr_map->CanMapPtr(ptr, field->type))
            {
                return;
            }
        }

        // Comma separator for multiple fields
        if (field_written)
        {
            out.WriteChar(',');
            NewLine(out, flags);
        }

        // Write the field name and object
        SaveString(out, field->name.text);
        out.WriteChar(':');
        SaveFieldObject(out, object + field->offset, field, ptr_map, flags, transient_flags);
        field_written = true;
    }

    void SaveClassFields(clutl::WriteBuffer& out, const char* object, const clcpp::Class* class_type, clutl::IPtrMap* ptr_map,
                         unsigned int& flags, bool& field_written, unsigned int transient_flags)
    {
        const clcpp::CArray<const clcpp::Field*>& fields = class_type->fields;
        unsigned int nb_fields = fields.size;

        if ((flags & clutl::JSONFlags::SORT_CLASS_FIELDS_BY_OFFSET) != 0)
        {
            int last_field_offset = -1;

            // Save fields in offset-sorted order
            // This is a brute-force O(n^2) implementation as I don't want to allocate any heap memory or stack memory
            // with an upper bound. As the source array is also const and needs to be used elsewhere, sorting in-place
            // is also not an option.
            for (unsigned int i = 0; i < nb_fields; i++)
            {
                int lowest_field_offset = 0x7FFFFFFF;
                int lowest_field_index = -1;

                // Search for the next field with the lowest offset
                for (unsigned int j = 0; j < nb_fields; j++)
                {
                    // Skip transient fields
                    const clcpp::Field* field = fields[j];
                    if ((field->flag_attributes & transient_flags) != 0)
                    {
                        continue;
                    }

                    if (field->offset > last_field_offset && field->offset < lowest_field_offset)
                    {
                        lowest_field_offset = field->offset;
                        lowest_field_index = j;
                    }
                }

                // Use of transient implies not all fields may be serialised
                if (lowest_field_index != -1)
                {
                    const clcpp::Field* field = fields[lowest_field_index];
                    SaveClassField(out, object, field, ptr_map, flags, field_written, transient_flags);
                    last_field_offset = lowest_field_offset;
                }
            }
        }

        else
        {
            // Save fields in array order
            for (unsigned int i = 0; i < nb_fields; i++)
            {
                // Skip transient fields
                const clcpp::Field* field = fields[i];
                if ((field->flag_attributes & transient_flags) != 0)
                {
                    continue;
                }

                SaveClassField(out, object, field, ptr_map, flags, field_written, transient_flags);
            }
        }
    }

    void SaveClass(clutl::WriteBuffer& out, const char* object, const clcpp::Type* type, clutl::IPtrMap* ptr_map,
                   unsigned int& flags, bool& field_written, unsigned int transient_flags)
    {
        if (type->kind == clcpp::Primitive::KIND_CLASS)
        {
            // Skip transient classes
            const clcpp::Class* class_type = type->AsClass();
            if ((class_type->flag_attributes & transient_flags) != 0)
            {
                return;
            }

            // Save body of the class
            SaveClassFields(out, object, class_type, ptr_map, flags, field_written, transient_flags);
        }

        // Recurse into base types
        for (unsigned int i = 0; i < type->base_types.size; i++)
        {
            const clcpp::Type* base_type = type->base_types[i];
            SaveClass(out, object, base_type, ptr_map, flags, field_written, transient_flags);
        }
    }

    void SaveClass(clutl::WriteBuffer& out, const char* object, const clcpp::Class* class_type, clutl::IPtrMap* ptr_map,
                   unsigned int flags, unsigned int transient_flags)
    {
        // Skip transient classes
        if ((class_type->flag_attributes & transient_flags) != 0)
        {
            return;
        }

        // Is there a custom loading function for this class?
        if ((class_type->flag_attributes & attrFlag_CustomSave) != 0)
        {
            // Look it up
            static unsigned int hash = clcpp::internal::HashNameString("save_json");
            if (const clcpp::Attribute* attr = clcpp::FindPrimitive(class_type->attributes, hash))
            {
                const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();

                // Call the function to generate an output token
                clutl::JSONToken token;
                clcpp::CallFunction(static_cast<const clcpp::Function*>(name_attr->primitive), clcpp::ByRef(token), object);

                // Serialise appropriately
                switch (token.type)
                {
                case clutl::JSON_TOKEN_STRING:
                    SaveString(out, token.val.string, token.val.string + token.length);
                    break;
                case clutl::JSON_TOKEN_INTEGER:
                    SaveInteger(out, token.val.integer);
                    break;
                case clutl::JSON_TOKEN_DECIMAL:
                    SaveDecimal(out, token.val.decimal, flags);
                    break;
                default:
                    clcpp::internal::Assert(false && "Invalid token output type");
                }

                return;
            }
        }

        // Call any attached pre-save function
        if ((class_type->flag_attributes & attrFlag_PreSave) != 0)
        {
            static unsigned int hash = clcpp::internal::HashNameString("pre_save");
            if (const clcpp::Attribute* attr = clcpp::FindPrimitive(class_type->attributes, hash))
            {
                const clcpp::PrimitiveAttribute* name_attr = attr->AsPrimitiveAttribute();
                if (name_attr->primitive != nullptr)
                {
                    clcpp::CallFunction(static_cast<const clcpp::Function*>(name_attr->primitive), object);
                }
            }
        }

        bool field_written = false;
        OpenScope(out, flags);
        SaveClass(out, object, class_type, ptr_map, flags, field_written, transient_flags);
        CloseScope(out, flags);
    }

    void SaveContainer(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, const clcpp::Type* type,
                       clutl::IPtrMap* ptr_map, unsigned int flags, unsigned int transient_flags)
    {
        // Construct a read iterator and leave early if there are no elements
        clcpp::ReadIterator reader;
        reader.Initialise(type, object);
        if (reader.m_Count == 0)
        {
            out.WriteStr("[]");
            return;
        }

        SaveContainer(out, reader, field, ptr_map, flags, transient_flags);
    }

    void SaveObject(clutl::WriteBuffer& out, const char* object, const clcpp::Field* field, const clcpp::Type* type,
                    clutl::IPtrMap* ptr_map, unsigned int flags, unsigned int transient_flags)
    {
        if (type->ci != nullptr)
        {
            SaveContainer(out, object, field, type, ptr_map, flags, transient_flags);
            return;
        }

        // Dispatch to a save function based on kind
        switch (type->kind)
        {
        case clcpp::Primitive::KIND_TYPE:
            SaveType(out, object, type, flags);
            break;

        case clcpp::Primitive::KIND_ENUM:
            SaveEnum(out, object, type->AsEnum());
            break;

        case clcpp::Primitive::KIND_CLASS:
            SaveClass(out, object, type->AsClass(), ptr_map, flags, transient_flags);
            break;

        case clcpp::Primitive::KIND_TEMPLATE_TYPE:
            SaveContainer(out, object, field, type, ptr_map, flags, transient_flags);
            break;

        default:
            clcpp::internal::Assert(false && "Invalid primitive kind for type");
        }
    }
}

CLCPP_API void clutl::SaveJSON(WriteBuffer& out, const void* object, const clcpp::Type* type, IPtrMap* ptr_map,
                               unsigned int flags, unsigned int transient_flags)
{
    SetupTypeDispatchLUT();
    SaveObject(out, static_cast<const char*>(object), nullptr, type, ptr_map, flags, transient_flags);
}

CLCPP_API void clutl::SaveJSON(WriteBuffer& out, const void* object, const clcpp::Field* field, IPtrMap* ptr_map,
                               unsigned int flags, unsigned int transient_flags)
{
    SetupTypeDispatchLUT();
    SaveFieldObject(out, static_cast<const char*>(object), field, ptr_map, flags, transient_flags);
}

static void SetupTypeDispatchLUT()
{
    if (!g_TypeDispatchLUTReady)
    {
        // Add all integers
        AddTypeDispatch("bool", SaveIntegerWithCast<bool>, LoadIntegerBool, LoadDecimalBool);
        AddTypeDispatch("char", SaveIntegerWithCast<char>, LoadIntegerWithCast<char>, LoadDecimalWithCast<char>);
        AddTypeDispatch("wchar_t", SaveUnsignedIntegerWithCast<wchar_t>, LoadIntegerWithCast<wchar_t>,
                        LoadDecimalWithCast<wchar_t>);
        AddTypeDispatch("unsigned char", SaveUnsignedIntegerWithCast<unsigned char>, LoadIntegerWithCast<unsigned char>,
                        LoadDecimalWithCast<unsigned char>);
        AddTypeDispatch("short", SaveIntegerWithCast<short>, LoadIntegerWithCast<short>, LoadDecimalWithCast<short>);
        AddTypeDispatch("unsigned short", SaveUnsignedIntegerWithCast<unsigned short>, LoadIntegerWithCast<unsigned short>,
                        LoadDecimalWithCast<unsigned short>);
        AddTypeDispatch("int", SaveIntegerWithCast<int>, LoadIntegerWithCast<int>, LoadDecimalWithCast<int>);
        AddTypeDispatch("unsigned int", SaveUnsignedIntegerWithCast<unsigned int>, LoadIntegerWithCast<unsigned int>,
                        LoadDecimalWithCast<unsigned int>);
        AddTypeDispatch("long", SaveIntegerWithCast<long>, LoadIntegerWithCast<long>, LoadDecimalWithCast<long>);
        AddTypeDispatch("unsigned long", SaveUnsignedIntegerWithCast<unsigned long>, LoadIntegerWithCast<unsigned long>,
                        LoadDecimalWithCast<unsigned long>);
        AddTypeDispatch("long long", SaveIntegerWithCast<long long>, LoadIntegerWithCast<long long>,
                        LoadDecimalWithCast<long long>);
        AddTypeDispatch("unsigned long long", SaveUnsignedIntegerWithCast<unsigned long long>,
                        LoadIntegerWithCast<unsigned long long>, LoadDecimalWithCast<unsigned long long>);

        // Add all decimals
        AddTypeDispatch("float", SaveFloat, LoadIntegerWithCast<float>, LoadDecimalWithCast<float>);
        AddTypeDispatch("double", SaveDouble, LoadIntegerWithCast<double>, LoadDecimalWithCast<double>);

        g_TypeDispatchLUTReady = true;
    }
}
