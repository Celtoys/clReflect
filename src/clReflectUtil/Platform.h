

// Private, external platform dependencies for clReflectUtil
// All dependencies must come through here without this header file invoking platform-specific header files.


// Standard C library function, convert string to double-precision number
// http://pubs.opengroup.org/onlinepubs/007904975/functions/strtod.html
extern "C" double strtod(const char* s00, char** se);


// Standard C library function, copy bytes
// http://pubs.opengroup.org/onlinepubs/009695399/functions/memcpy.html
extern "C" void* memcpy(void* dst, const void* src, unsigned int size);


// Non-standard, writes at most n bytes to dest with printf formatting
// On Win32/MSVC, this usually maps to _snprintf_s
extern "C" int snprintf(char* dest, unsigned int n, const char* fmt, ...);


// Share library (DLL/SO) implementations
void* LoadSharedLibrary(const char* filename);
void* GetSharedLibraryFunction(void* handle, const char* function_name);
void FreeSharedLibrary(void* handle);