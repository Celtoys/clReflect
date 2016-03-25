
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#include <clutl/Module.h>
#include <clcpp/clcpp.h>


#if defined(CLCPP_PLATFORM_WINDOWS)

	// Windows-specific module loading and inspection functions
	typedef int (__stdcall *FunctionPtr)();
	extern "C" __declspec(dllimport) void* __stdcall LoadLibraryA(const char* lpLibFileName);
	extern "C" __declspec(dllimport) FunctionPtr __stdcall GetProcAddress(void* module, const char* lpProcName);
	extern "C" __declspec(dllimport) int __stdcall FreeLibrary(void* hLibModule);

#elif defined(CLCPP_PLATFORM_POSIX)

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
	void* LoadSharedLibrary(const char* filename)
	{
	#if defined(CLCPP_PLATFORM_WINDOWS)
		return LoadLibraryA(filename);
	#elif defined(CLCPP_PLATFORM_POSIX)
		return dlopen(filename, LOADING_FLAGS);
	#endif
	}


	void* GetSharedLibraryFunction(void* handle, const char* function_name)
	{
	#if defined(CLCPP_PLATFORM_WINDOWS)
		return GetProcAddress(handle, function_name);
	#elif defined(CLCPP_PLATFORM_POSIX)
		return dlsym(handle, function_name);
	#endif
	}


	void FreeSharedLibrary(void* handle)
	{
	#if defined(CLCPP_PLATFORM_WINDOWS)
		FreeLibrary(handle);
	#elif defined(CLCPP_PLATFORM_POSIX)
		dlclose(handle);
	#endif
	}
}


clutl::Module::Module()
	: m_Handle(0)
	, m_HostReflectionDB(0)
	, m_ReflectionDB(0)
{
}


clutl::Module::~Module()
{
	if (m_Handle != 0)
		FreeSharedLibrary(m_Handle);
}


bool clutl::Module::Load(clcpp::Database* host_db, const char* filename)
{
	// Load the DLL
	m_Handle = LoadSharedLibrary(filename);
	if (m_Handle == 0)
		return false;

	// Keep this around for registering interface implementations
	clcpp::internal::Assert(host_db != 0);
	m_HostReflectionDB = host_db;

	// Get the module reflection database
	typedef clcpp::Database* (*GetReflectionDatabaseFunc)();
	GetReflectionDatabaseFunc GetReflectionDatabase = (GetReflectionDatabaseFunc)GetFunction("GetReflectionDatabase");
	if (GetReflectionDatabase)
		m_ReflectionDB = GetReflectionDatabase();

	return true;
}


void* clutl::Module::GetFunction(const char* name) const
{
	clcpp::internal::Assert(m_Handle != 0);
	return GetSharedLibraryFunction(m_Handle, name);
}
