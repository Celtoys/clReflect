

// Private, external platform dependencies for clReflectUtil
// All dependencies must come through here without this header file invoking platform-specific header files.


// check for operating systems
#if defined(_WINDOWS) || defined(_WIN32)

	#define CLUTL_PLATFORM_WINDOWS

#elif defined(__linux__) || defined(__APPLE__)

	#define CLUTL_PLATFORM_POSIX

#endif


// Standard C library function, convert string to double-precision number
// http://pubs.opengroup.org/onlinepubs/007904975/functions/strtod.html
extern "C" double strtod(const char* s00, char** se);


// Standard C library function, copy bytes
// http://pubs.opengroup.org/onlinepubs/009695399/functions/memcpy.html
extern "C" void* memcpy(void* dst, const void* src, unsigned int size);


// Non-standard, writes at most n bytes to dest with printf formatting
#if defined(CLUTL_PLATFORM_WINDOWS)
	extern "C" int _snprintf(char* dest, unsigned int n, const char* fmt, ...);
	#define snprintf _snprintf
#else
	extern "C" int snprintf(char* dest, unsigned int n, const char* fmt, ...);
#endif


// Share library (DLL/SO) implementations
void* LoadSharedLibrary(const char* filename);
void* GetSharedLibraryFunction(void* handle, const char* function_name);
void FreeSharedLibrary(void* handle);