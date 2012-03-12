
#include "Platform.h"


#if defined(_WINDOWS) || defined(_WIN32)
	// Windows-specific module loading and inspection functions
	typedef int (__stdcall *FunctionPtr)();
	extern "C" __declspec(dllimport) void* __stdcall LoadLibraryA(const char* lpLibFileName);
	extern "C" __declspec(dllimport) FunctionPtr __stdcall GetProcAddress(void* module, const char* lpProcName);
	extern "C" __declspec(dllimport) int __stdcall FreeLibrary(void* hLibModule);
#endif


void* LoadSharedLibrary(const char* filename)
{
	void* handle = 0;
#if defined(_WINDOWS) || defined(_WIN32)
	handle = LoadLibraryA(filename);
#endif
	return handle;
}


void* GetSharedLibraryFunction(void* handle, const char* function_name)
{
	void* function = 0;
#if defined(_WINDOWS) || defined(_WIN32)
	function = GetProcAddress(handle, function_name);
#endif
	return function;
}


void FreeSharedLibrary(void* handle)
{
#if defined(_WINDOWS) || defined(_WIN32)
	FreeLibrary(handle);
#endif
}