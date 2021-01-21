
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include "MapFileParser.h"

#if defined(CLCPP_USING_MSVC)

// clang-format off
	#define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <DbgHelp.h>
// clang-format on

#else

    // For GCC and clang, use cxxabi to demangle names
    #include <cxxabi.h>
    #include <stack>

#endif // CLCPP_USING_MSVC

#include <clReflectCore/Database.h>
#include <clReflectCore/FileUtils.h>
#include <clReflectCore/Logging.h>

#include <cstdio>
#include <cstring>

namespace
{
    const char* ConsumeParameterToken(const char* text, char* dest, int dest_size)
    {
        char* end = dest + dest_size;
        while (*text && *text != ' ' && *text != ',' && *text != ')' && *text != '<' && *text != '>' && dest != end)
        {
            *dest++ = *text++;
        }
        *dest = 0;
        return text;
    }

    cldb::Field MatchParameter(cldb::Database& db, const char*& ptr, const char* end, bool& is_this_call)
    {
        // TODO: Not exactly proud of this parsing code - started thinking it would be a simple problem
        // but the unseen complexity forced it to grow oddly. It works, though. Needs a rewrite.

        cldb::Field parameter;

        const char* skip_tokens[] = {// Argument passing specifications
                                     "__cdecl", "__stdcall", "__fastcall",
                                     // Type modifiers
                                     "struct", "class", "enum"};

        char type_name[1024] = {0};
        char token[1024] = {0};
        is_this_call = false;

        // Loop reading tokens irrespective of order. Note that this parsing strategy won't distinguish between
        // the type of const-qualifier. However, only one mode of qualification is currently supported so this
        // will suffice for now.
        bool parse = true;
        while (parse && ptr < end)
        {
            ptr = ConsumeParameterToken(ptr, token, sizeof(token));
            ptr = SkipWhitespace(ptr);

            // Check for modifiers
            if (token[0] == '&')
            {
                parameter.qualifier.op = cldb::Qualifier::REFERENCE;
            }
            else if (token[0] == '*')
            {
                parameter.qualifier.op = cldb::Qualifier::POINTER;
            }

            // Check for const qualification
            else if (!strcmp(token, "const"))
            {
                parameter.qualifier.is_const = true;
            }

            // Mark this calls so that we can add the this parameter first
            else if (!strcmp(token, "__thiscall"))
            {
                is_this_call = true;
            }

            // Check for any type prefixes
            else if (!strcmp(token, "unsigned") || !strcmp(token, "signed"))
            {
                strcpy(type_name, token);
                strcat(type_name, " ");
            }

            // Clang will rewrite any MS extension types it finds in the AST as their correct C++ equivalents.
            // UnDecorateSymbolName will preserve these, however.
            // We need to ensure we are using the same type names as the Clang AST to provide a valid match.
            else if (!strcmp(token, "__int64"))
            {
                strcat(type_name, "long long");
            }

            else
            {
                // First check to see if this token is to be ignored
                bool skip = false;
                for (int i = 0; i < sizeof(skip_tokens) / sizeof(skip_tokens[0]); i++)
                {
                    if (!strcmp(token, skip_tokens[i]))
                    {
                        skip = true;
                        break;
                    }
                }

                // What's remaining must be the type name
                if (skip == false)
                    strcat(type_name, token);
            }

            // Need template chars to mark specific token boundaries (for keyword skipping)
            // yet be added to the function signature
            if (*ptr == '<' || *ptr == '>')
            {
                token[0] = *ptr;
                token[1] = 0;
                strcat(type_name, token);
                ptr++;
            }

            if (*ptr == ',' || *ptr == ')')
            {
                ptr++;
                break;
            }
        }

        parameter.type = db.GetName(type_name);
        return parameter;
    }

    bool IsVoidParameter(const cldb::Field& field)
    {
        return field.qualifier.op == cldb::Qualifier::VALUE && field.type.text == "void";
    }

    bool IsConstructFunction(const std::string& function_name)
    {
        return startswith(function_name, "clcppConstructObject");
    }

    bool IsDestructFunction(const std::string& function_name)
    {
        return startswith(function_name, "clcppDestructObject");
    }

    bool IsGetTypeNameHashFunction(const std::string& function_name)
    {
        return startswith(function_name, "clcpp::GetTypeNameHash<");
    }

    bool IsGetTypeFunction(const std::string& function_name)
    {
        return startswith(function_name, "clcpp::GetType<");
    }

    bool AddFunctionAddress(cldb::Database& db, const std::string& function_name, const std::string& function_signature,
                            clcpp::pointer_type function_address, bool is_this_call, bool is_const)
    {
        if (function_address == 0)
            return false;

        // Find where the return type ends
        size_t func_pos = function_signature.find(function_name);
        if (func_pos == std::string::npos)
        {
            LOG(main, ERROR, "Couldn't locate function name in signature for '%s'", function_name.c_str());
            return false;
        }

        // Isolate the parameters in the signature
        size_t l_pos = function_signature.find('(', func_pos);
        if (l_pos == std::string::npos)
        {
            LOG(main, ERROR, "Couldn't locate left bracket in signature for '%s'", function_name.c_str());
            return false;
        }
        size_t r_pos = function_signature.find(')', l_pos);
        if (r_pos == std::string::npos)
        {
            LOG(main, ERROR, "Couldn't locate right bracket in signature for '%s'", function_name.c_str());
            return false;
        }

        std::vector<cldb::Field> parameters;
        if (is_this_call)
        {
            // Find the end of the type name
            size_t rsep = function_name.rfind("::");
            if (rsep == std::string::npos)
            {
                LOG(main, ERROR, "Function declaration says it's __thiscall but no type found in the name of '%s'",
                    function_name.c_str());
                return false;
            }

            // Construct the type name
            char type_name[1024];
            strncpy(type_name, function_name.c_str(), rsep);
            type_name[rsep] = 0;

            // Add the this parameter at the beginning
            cldb::Field this_parameter;
            this_parameter.type = db.GetName(type_name);
            this_parameter.qualifier.op = cldb::Qualifier::POINTER;
            this_parameter.qualifier.is_const = is_const;
            parameters.push_back(this_parameter);
        }

        // Parse the parameters
        const char* ptr = function_signature.c_str() + l_pos + 1;
        const char* end = function_signature.c_str() + r_pos;
        while (ptr < end)
        {
            cldb::Field parameter = MatchParameter(db, ptr, end, is_this_call);
            if (!IsVoidParameter(parameter))
                parameters.push_back(parameter);
        }

        // Calculate the ID of the matching function
        cldb::u32 unique_id = cldb::CalculateFunctionUniqueID(parameters);

        // Search through all functions of the same name
        cldb::u32 function_hash = clcpp::internal::HashNameString(function_name.c_str());
        cldb::DBMap<cldb::Function>::range functions = db.m_Functions.equal_range(function_hash);
        for (cldb::DBMap<cldb::Function>::iterator i = functions.first; i != functions.second; ++i)
        {
            // Assign the function address when the unique IDs match
            cldb::Function& function = i->second;
            if (function.unique_id == unique_id)
            {
                function.address = function_address;
                return true;
            }
        }

        return false;
    }

    size_t SkipTypePrefix(const std::string& text, size_t pos)
    {
        if (!strncmp(text.c_str() + pos, "struct ", sizeof("struct")))
            pos += sizeof("struct");
        if (!strncmp(text.c_str() + pos, "class ", sizeof("class")))
            pos += sizeof("class");
        if (!strncmp(text.c_str() + pos, "enum ", sizeof("enum")))
            pos += sizeof("enum");
        return pos;
    }

    void AddGetTypeAddress(cldb::Database& db, const std::string& function_name, clcpp::pointer_type function_address,
                           bool is_get_type)
    {
        if (function_address == 0)
            return;

        // Isolate the template parameter list
        size_t pos = function_name.find('<');
        if (pos == std::string::npos)
        {
            LOG(main, ERROR, "Couldn't locate opening angle bracket of the GetType function");
            return;
        }
        pos++;

        // Skip the prefix
        pos = SkipTypePrefix(function_name, pos);

        // Locate the end of the typename of the template parameter
        size_t end_pos = function_name.find('>', pos);
        if (end_pos == std::string::npos)
        {
            LOG(main, ERROR, "Couldn't locate closing angle bracket of the GetType function");
            return;
        }

        // Generate the name for the type
        std::string type_name_str = function_name.substr(pos, end_pos - pos);
        cldb::Name type_name = db.GetName(type_name_str.c_str());
        if (type_name.hash == 0)
        {
            LOG(main, ERROR, "GetType can't be used for unreflected '%s' type", type_name_str.c_str());
            return;
        }

        // Add to the database
        if (is_get_type)
            db.m_GetTypeFunctions[type_name.hash].get_type_address = function_address;
        else
            db.m_GetTypeFunctions[type_name.hash].get_typename_address = function_address;
    }

    void AddClassImplFunction(cldb::Database& db, const std::string& function_signature, clcpp::pointer_type function_address,
                              bool is_constructor)
    {
        if (function_address == 0)
            return;

        // Isolate the parameter list
        size_t pos = function_signature.find('(');
        if (pos == std::string::npos)
        {
            LOG(main, ERROR, "Couldn't locate opening bracket of class impl function");
            return;
        }
        pos++;

        // Skip the prefix
        pos = SkipTypePrefix(function_signature, pos);

        // Locate the end of the typename of the first parameter by checking for its
        // pointer spec and accounting for whitespace
        size_t end_pos = function_signature.find('*', pos);
        if (end_pos == std::string::npos)
        {
            LOG(main, ERROR, "Couldn't locate pointer character for first parameter of class impl function");
            return;
        }
        while (function_signature[end_pos] == ' ' || function_signature[end_pos] == '*')
            end_pos--;

        // Generate the names for the parameter
        std::string parameter_type_name_str = function_signature.substr(pos, end_pos - pos + 1);
        cldb::Name parameter_type_name = db.GetName(parameter_type_name_str.c_str());
        cldb::Name parameter_name = db.GetName("this");

        // Generate a name for the new function
        std::string function_name_str = parameter_type_name_str + "::";
        if (is_constructor)
            function_name_str += "ConstructObject";
        else
            function_name_str += "DestructObject";
        cldb::Name function_name = db.GetName(function_name_str.c_str());

        // Create the parameter
        cldb::Field parameter(parameter_name, function_name, parameter_type_name,
                              cldb::Qualifier(cldb::Qualifier::POINTER, false), 0);

        // Generate a unique ID that binds the function and parameter together
        std::vector<cldb::Field> parameters;
        parameters.push_back(parameter);
        cldb::u32 unique_id = cldb::CalculateFunctionUniqueID(parameters);

        // Create the function and bind the parameter to it
        cldb::Function function(function_name, parameter_type_name, unique_id);
        parameter.parent_unique_id = unique_id;

        // Record the transient function address that will be exported
        function.address = function_address;

        // Add the new primitives to the database
        db.AddPrimitive(parameter);
        db.AddPrimitive(function);
    }

    void AddConstructFunction(cldb::Database& db, const std::string& function_signature, clcpp::pointer_type function_address)
    {
        AddClassImplFunction(db, function_signature, function_address, true);
    }

    void AddDestructFunction(cldb::Database& db, const std::string& function_signature, clcpp::pointer_type function_address)
    {
        AddClassImplFunction(db, function_signature, function_address, false);
    }

// MSVC map parsing functions
#if defined(CLCPP_USING_MSVC)
    bool InitialiseSymbolHandler()
    {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        if (!SymInitialize(GetCurrentProcess(), 0, TRUE))
        {
            LOG(main, ERROR, "Couldn't initialise symbol handler - no function addresses will be available!");
            return false;
        }

        return true;
    }

    void ShutdownSymbolHandler()
    {
        SymCleanup(GetCurrentProcess());
    }

    std::string UndecorateFunctionName(const char* token)
    {
        char function_name[1024];
        UnDecorateSymbolName(token, function_name, sizeof(function_name), UNDNAME_NAME_ONLY);
        return function_name;
    }

    std::string UndecorateFunctionSignature(const char* token)
    {
        char function_signature[1024];
        UnDecorateSymbolName(token, function_signature, sizeof(function_signature),
                             UNDNAME_COMPLETE | UNDNAME_NO_ACCESS_SPECIFIERS | UNDNAME_NO_ALLOCATION_MODEL |
                                 UNDNAME_NO_MEMBER_TYPE | UNDNAME_NO_SPECIAL_SYMS | UNDNAME_NO_THROW_SIGNATURES);
        return function_signature;
    }

    clcpp::pointer_type ParseAddressField(const char* line, const char* function_name)
    {
        // First parse the address as hex
        char token[1024];
        line = SkipWhitespace(line);
        line = ConsumeToken(line, ' ', token, sizeof(token));
        clcpp::pointer_type function_address = hextoi(token);

        // Double-check that the map file knows this is a function
        line = SkipWhitespace(line);
        if (line[0] != 'f')
        {
            LOG(main, ERROR, "Function '%s' is not a function symbol in the map file", function_name);
            return 0;
        }

        return function_address;
    }

    void ParseMSVCMapFile(const char* filename, cldb::Database& db, clcpp::pointer_type& base_address)
    {
        if (!InitialiseSymbolHandler())
        {
            return;
        }

        FILE* fp = fopen(filename, "rb");
        if (fp == 0)
        {
            return;
        }

        bool public_symbols = false;
        while (const char* line = ReadLine(fp))
        {
            if (public_symbols)
            {
                char token[1024];

                // Consume everything up to the function name
                line = SkipWhitespace(line);
                line = ConsumeToken(line, ' ', token, sizeof(token));
                line = SkipWhitespace(line);
                line = ConsumeToken(line, ' ', token, sizeof(token));

                // Undecorate the symbol name alone and see if it's a known clcpp function
                std::string function_name = UndecorateFunctionName(token);
                if (IsConstructFunction(function_name))
                {
                    std::string function_signature = UndecorateFunctionSignature(token);
                    clcpp::pointer_type function_address = ParseAddressField(line, function_name.c_str());
                    AddConstructFunction(db, function_signature, function_address);
                }
                else if (IsDestructFunction(function_name))
                {
                    std::string function_signature = UndecorateFunctionSignature(token);
                    clcpp::pointer_type function_address = ParseAddressField(line, function_name.c_str());
                    AddDestructFunction(db, function_signature, function_address);
                }
                else if (IsGetTypeFunction(function_name))
                {
                    clcpp::pointer_type function_address = ParseAddressField(line, function_name.c_str());
                    AddGetTypeAddress(db, function_name, function_address, true);
                }
                else if (IsGetTypeNameHashFunction(function_name))
                {
                    clcpp::pointer_type function_address = ParseAddressField(line, function_name.c_str());
                    AddGetTypeAddress(db, function_name, function_address, false);
                }

                // Otherwise see if it's a function in the database
                else if (const cldb::Function* function = db.GetFirstPrimitive<cldb::Function>(function_name.c_str()))
                {
                    std::string function_signature = UndecorateFunctionSignature(token);
                    clcpp::pointer_type function_address = ParseAddressField(line, function_name.c_str());

                    bool is_this_call = false;
                    const char* ptr = function_signature.c_str();
                    size_t func_pos = function_signature.find(function_name);

                    if (func_pos == std::string::npos)
                    {
                        LOG(main, ERROR, "Couldn't locate function name in signature for '%s'", function_name.c_str());
                        return;
                    }

                    // Skip the return parameter as it can't be used to overload a function
                    cldb::Field returnValue = MatchParameter(db, ptr, ptr + func_pos, is_this_call);
                    AddFunctionAddress(db, function_name, function_signature, function_address, is_this_call,
                                       returnValue.qualifier.is_const);
                }
            }

            // Look for the start of the public symbols descriptors
            else if (strstr(line, "  Address"))
            {
                ReadLine(fp);
                public_symbols = true;
            }

            // Parse the preferred load address
            if (base_address == 0 && strstr(line, "Preferred load address is "))
            {
                line += sizeof("Preferred load address is ");
                char token[32];
                ConsumeToken(line, '\r', token, sizeof(token));

    #if defined(CLCPP_USING_64_BIT)
                base_address = hextoi64(token);
    #else
                base_address = hextoi(token);
    #endif
            }
        }
        fclose(fp);
        ShutdownSymbolHandler();
    }
#endif // CLCPP_USING_MSVC

// GCC/clang parsing functions
// For GCC/clang, we can use abi::__cxa_demangle to do name demangling, but the result
// is the whole function signature, so we provide hand-written functions to parse function
// name from signature here.
#if defined(CLCPP_USING_GNUC)
    // Finds a sequence such that it will not appear in given function signature
    std::string FindRandomString(const std::string& signature)
    {
        static const char* NUMBER_TABLE = "0123456789";
        std::string res = "r?An_D";

        while (signature.find(res) != std::string::npos)
        {
            res += NUMBER_TABLE[rand() % 10];
        }

        return res;
    }

    // Appends a number as a substring to a given string
    std::string AppendNumberToString(const std::string& str, unsigned int num)
    {
        return str + itoa(num);
    }

    // Replaces all occurences of src to dst in original_string
    void ReplaceAll(std::string& original_string, const std::string& src, const std::string& dst)
    {
        size_t start_pos = 0;
        size_t find_pos = original_string.find(src, start_pos);
        while (find_pos != std::string::npos)
        {
            original_string.replace(find_pos, src.size(), dst);
            start_pos = find_pos + dst.size();

            find_pos = original_string.find(src, start_pos);
        }
    }

    // Replaces all occurences of original_string to a unique, newly generated string in function signature
    void ReplaceOccurence(std::string& signature, std::stack<std::pair<std::string, std::string>>& replace_stack,
                          const std::string& base_replace_string, const std::string& original_string)
    {
        std::string replaced_string = AppendNumberToString(base_replace_string, replace_stack.size());

        ReplaceAll(signature, original_string, replaced_string);
        replace_stack.push(std::make_pair(original_string, replaced_string));
    }

    // Replaces all string pieces that starts with left_pattern, and ends with right_pattern using
    // a newly generated string in function signature
    void ReplacePattern(std::string& signature, std::stack<std::pair<std::string, std::string>>& replace_stack,
                        const std::string& base_replace_string, const std::string& left_pattern, const std::string& right_pattern)
    {
        size_t end_pos = std::string::npos;
        size_t find_pos = signature.rfind(left_pattern, end_pos);
        while (find_pos != std::string::npos)
        {
            size_t closing_pos = signature.find(right_pattern, find_pos + left_pattern.size());
            if (closing_pos == std::string::npos)
            {
                // no ending position, error occurs
                return;
            }
            std::string original_string = signature.substr(find_pos, closing_pos + right_pattern.size() - find_pos);
            ReplaceOccurence(signature, replace_stack, base_replace_string, original_string);

            end_pos = find_pos - 1;

            find_pos = signature.rfind(left_pattern, end_pos);
        }
    }

    // Restores all previously replaced string pieces
    std::string RestoreReplacedString(std::string str, std::stack<std::pair<std::string, std::string>>& replace_stack)
    {
        while (!replace_stack.empty())
        {
            const std::pair<std::string, std::string>& pair = replace_stack.top();
            ReplaceAll(str, pair.second, pair.first);

            replace_stack.pop();
        }
        return str;
    }

    // Parses function name from a function signature following Itanium C++ ABI
    std::string ParseFunctionName(std::string signature)
    {
        std::stack<std::pair<std::string, std::string>> replace_stack;
        std::string base_replace_string = FindRandomString(signature);

        // Replaces all occurances of "(anonymous namespace)"
        ReplaceOccurence(signature, replace_stack, base_replace_string, "(anonymous namespace)");

        // TODO: in the next two patterns, the "..." field can not contain any parentheses, fix this.

        // Replaces "&(...(...))" types of strings
        // This is the function specialization in templates, an example of this is:
        // (anonymous namespace)::BinarySearch<clcpp::Primitive const*, clcpp::Primitive const*,
        //   &((anonymous namespace)::GetPrimitivePtrHash(clcpp::Primitive const*))>
        ReplacePattern(signature, replace_stack, base_replace_string, "&(", "))");

        // Replaces "(*)(...)" types of strings
        // This is the function pointer types, an example is:
        // (anonymous namespace)::AddTypeDispatch(char const*, void (*)(clutl::WriteBuffer&, char const*, unsigned int),
        //     void (*)(char*, long long), void (*)(char*, double))
        ReplacePattern(signature, replace_stack, base_replace_string, "(*)(", ")");

        // Replaces all template arguments
        ReplacePattern(signature, replace_stack, base_replace_string, "<", ">");

        // Now searches for starting of parameter list
        size_t parenth_pos = signature.find('(');
        if (parenth_pos == std::string::npos)
        {
            parenth_pos = signature.size() + 1;
        }

        // Since templates are all gone, now the last whitespace preceeding
        // left ( should be the one to separate return type with function name
        size_t function_name_start_pos = signature.rfind(' ', parenth_pos);
        if (function_name_start_pos == std::string::npos)
        {
            // no return type(void)
            function_name_start_pos = 0;
        }
        else
        {
            // move start position from ' ' to the first character of function
            function_name_start_pos++;
        }

        // searches for preceding "operator" keyword
        if ((function_name_start_pos >= 9) && (signature.compare(function_name_start_pos - 9, 9, "operator ") == 0))
        {
            // includes preceding "operator " keyword in function name
            function_name_start_pos -= 9;
        }

        return RestoreReplacedString(signature.substr(function_name_start_pos, parenth_pos - function_name_start_pos),
                                     replace_stack);
    }

    void ProcessFunctionItem(cldb::Database& db, const std::string& function_signature, clcpp::pointer_type function_address)
    {
        std::string function_name = ParseFunctionName(function_signature);
        if (function_name.size() == 0)
        {
            LOG(main, ERROR, "Cannot parse function name from function signature '%s'", function_signature.c_str());
            return;
        }

        if (IsConstructFunction(function_name))
        {
            AddConstructFunction(db, function_signature, function_address);
        }
        else if (IsDestructFunction(function_name))
        {
            AddDestructFunction(db, function_signature, function_address);
        }
        else if (IsGetTypeFunction(function_name))
        {
            AddGetTypeAddress(db, function_name, function_address, true);
        }
        else if (IsGetTypeNameHashFunction(function_name))
        {
            AddGetTypeAddress(db, function_name, function_address, false);
        }
        // Otherwise see if it's a function in the database
        else if (const cldb::Function* function = db.GetFirstPrimitive<cldb::Function>(function_name.c_str()))
        {
            size_t const_pos = function_signature.rfind("const");
            const bool is_function_const = const_pos == (function_signature.size() - 5);

            // try to add as this call (member function of class / struct etc.)
            if (AddFunctionAddress(db, function_name, function_signature, function_address, true, is_function_const) == false)
            {
                LOG(main, WARNING,
                    "Couldn't store address for function with signature '%s' as this_call. Try to find static version of "
                    "function.\n",
                    function_signature.c_str());

                // if it is not a method then it has to be a static method or "not member function"
                const bool isAddressStored =
                    AddFunctionAddress(db, function_name, function_signature, function_address, false, is_function_const);
                if (isAddressStored == false)
                {
                    LOG(main, ERROR,
                        "Couldn't store address for function with signature '%s'. No static or \"this call\" function with same "
                        "unique id found ! \n",
                        function_signature.c_str());
                }
                else
                {
                    LOG(main, INFO,
                        "Static Function with same unique id found and address stored for function with signature '%s' ! \n",
                        function_signature.c_str());
                }
            }
        }
    }

    void ParseMacGCCMapFile(FILE* fp, cldb::Database& db, clcpp::pointer_type& base_address)
    {
        bool section_region = false;
        bool symbol_region = false;

        clcpp::pointer_type function_address;
        clcpp::size_type function_size;
        int file_id;
        char segment_buffer[1024];
        char signature_buffer[1024];

        while (const char* line = ReadLine(fp))
        {
            if (section_region)
            {
                if (sscanf(line, "0x%" CLCPP_POINTER_TYPE_HEX_FORMAT " 0x%" CLCPP_SIZE_TYPE_HEX_FORMAT " %s %s",
                           &function_address, &function_size, segment_buffer, signature_buffer) == 4)
                {
                    if (strcmp(signature_buffer, "__text") == 0)
                    {
                        base_address = function_address;
                    }
                }
            }
            else if (symbol_region)
            {
                if (sscanf(line, "0x%" CLCPP_POINTER_TYPE_HEX_FORMAT " 0x%" CLCPP_SIZE_TYPE_HEX_FORMAT " [%d] %s",
                           &function_address, &function_size, &file_id, signature_buffer) == 4)
                {
                    if (startswith(signature_buffer, "__"))
                    {
                        // function name starts with __
                        int status;
                        char* demangle_signature = abi::__cxa_demangle(signature_buffer + 1, 0, 0, &status);
                        if (status == 0)
                        {
                            // In generated map some items are noisy data, for example:
                            // "vtable for ArrayReadIterator"
                            // We do not need such lines
                            if (!strstr(demangle_signature, " for "))
                            {
                                ProcessFunctionItem(db, demangle_signature, function_address);
                            }
                        }
                        if (demangle_signature != NULL)
                        {
                            free(demangle_signature);
                        }
                    }
                }
            }

            if (strstr(line, "# Sections:"))
            {
                // section region
                ReadLine(fp);
                section_region = true;
                symbol_region = false;
            }
            else if (strstr(line, "# Symbols:"))
            {
                // symbol region
                ReadLine(fp);
                section_region = false;
                symbol_region = true;
            }
        }
    }

    void ParseLinuxGCCMapFile(FILE* fp, cldb::Database& db, clcpp::pointer_type& base_address)
    {
        bool text_region = false;

        clcpp::pointer_type function_address, function_size;
        char signature_buffer[1024];

        while (const char* line = ReadLine(fp))
        {
            if (text_region)
            {
                if ((sscanf(line, " 0x%" CLCPP_POINTER_TYPE_HEX_FORMAT " %s", &function_address, signature_buffer) == 2) &&
                    (signature_buffer[0] == '_'))
                {
                    int status;
                    char* demangle_signature = abi::__cxa_demangle(signature_buffer, 0, 0, &status);
                    if (status == 0)
                    {
                        if (!strstr(demangle_signature, " for "))
                        {
                            ProcessFunctionItem(db, demangle_signature, function_address);
                        }
                    }
                    if (demangle_signature != 0)
                    {
                        free(demangle_signature);
                    }
                }
            }
            else if (strstr(line, ".text") == line)
            {
                if (sscanf(line, ".text 0x%" CLCPP_POINTER_TYPE_HEX_FORMAT " 0x%" CLCPP_POINTER_TYPE_HEX_FORMAT,
                           &function_address, &function_size) == 2)
                {
                    // text section start address
                    base_address = function_address;
                    text_region = true;
                }
            }
        }
    }

    void ParseGCCMapFile(const char* filename, cldb::Database& db, clcpp::pointer_type& base_address)
    {
        FILE* fp = fopen(filename, "rb");
        if (fp == 0)
        {
            return;
        }

        const char* first_line = ReadLine(fp);
        if (startswith(first_line, "# Path:"))
        {
            ParseMacGCCMapFile(fp, db, base_address);
        }
        else if (startswith(first_line, "Archive member included because of file (symbol)"))
        {
            ParseLinuxGCCMapFile(fp, db, base_address);
        }
        else
        {
            LOG(main, ERROR, "Unknown format of gcc map file!");
        }

        fclose(fp);
    }
#endif // CLCPP_USING_GNUC
}

MapFileParser::MapFileParser(cldb::Database& db, const char* filename)
    : m_PreferredLoadAddress(0)
{
#if defined(CLCPP_USING_MSVC)
    ParseMSVCMapFile(filename, db, m_PreferredLoadAddress);
#else
    ParseGCCMapFile(filename, db, m_PreferredLoadAddress);
#endif // CLCPP_USING_MSVC
}
