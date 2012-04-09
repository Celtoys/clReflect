
#include "Platform.h"


// check for operating systems
#if defined(_WINDOWS) || defined(_WIN32)

	#define CLUTL_PLATFORM_WINDOWS

#elif defined(__linux__) || defined(__APPLE__)

	#define CLUTL_PLATFORM_POSIX

#endif


#if defined(CLUTL_PLATFORM_WINDOWS)
	// Windows-specific module loading and inspection functions
	typedef int (__stdcall *FunctionPtr)();
	extern "C" __declspec(dllimport) void* __stdcall LoadLibraryA(const char* lpLibFileName);
	extern "C" __declspec(dllimport) FunctionPtr __stdcall GetProcAddress(void* module, const char* lpProcName);
	extern "C" __declspec(dllimport) int __stdcall FreeLibrary(void* hLibModule);
#endif


#if defined(CLUTL_PLATFORM_POSIX)
	// We use POSIX-compatible dynamic linking loader interface, which
	// should be present on both Mac and Linux
	extern "C" int dlclose(void * __handle);
	extern "C" void * dlopen(const char * __path, int __mode);
	extern "C" void * dlsym(void * __handle, const char * __symbol);

	// TODO: check the loading flags when we can get this running, current
	// flag indicates RTLD_LAZY
	#define LOADING_FLAGS 0x1
#endif


void* LoadSharedLibrary(const char* filename)
{
	void* handle = 0;
#if defined(CLUTL_PLATFORM_WINDOWS)
	handle = LoadLibraryA(filename);
#elif defined(CLUTL_PLATFORM_POSIX)
	handle = dlopen(filename, LOADING_FLAGS);
#endif
	return handle;
}


void* GetSharedLibraryFunction(void* handle, const char* function_name)
{
	void* function = 0;
#if defined(CLUTL_PLATFORM_WINDOWS)
	function = GetProcAddress(handle, function_name);
#elif defined(CLUTL_PLATFORM_POSIX)
	function = dlsym(handle, function_name);
#endif
	return function;
}


void FreeSharedLibrary(void* handle)
{
#if defined(CLUTL_PLATFORM_WINDOWS)
	FreeLibrary(handle);
#elif defined(CLUTL_PLATFORM_POSIX)
	dlclose(handle);
#endif
}
