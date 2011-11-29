
#include <clutl/Module.h>
#include <clcpp/Database.h>


#ifdef _WINDOWS

	// Windows-specific module loading and inspection functions
	// TODO: Create a single IHost interface in clcpp and stick all this kind of stuff in there?
	typedef int (__stdcall *FunctionPtr)();
	extern "C" __declspec(dllimport) void* __stdcall LoadLibraryA(const char* lpLibFileName);
	extern "C" __declspec(dllimport) FunctionPtr __stdcall GetProcAddress(void* module, const char* lpProcName);
	extern "C" __declspec(dllimport) void __stdcall FreeLibrary(void* hLibModule);

#else

	// Non-functioning implementations for all other platforms
	void* LoadLibraryA(const char*) { return 0; }
	void* GetProcAddress(void*, const char*) { return 0; }
	void FreeLibrary(void*);

#endif


clutl::Module::Module()
	: m_Handle(0)
	, m_HostReflectionDB(0)
	, m_ReflectionDB(0)
{
}


clutl::Module::~Module()
{
	if (m_Handle != 0)
		FreeLibrary(m_Handle);
}


bool clutl::Module::Load(clcpp::Database* host_db, const char* filename)
{
	// Load the DLL
	m_Handle = LoadLibraryA(filename);
	if (m_Handle == 0)
		return false;

	// Keep this around for registering interface implementations
	clcpp::internal::Assert(host_db != 0);
	m_HostReflectionDB = host_db;

	// Get the module reflection database
	typedef clcpp::Database* (*GetReflectionDatabaseFunc)();
	GetReflectionDatabaseFunc GetReflectionDatabase = (GetReflectionDatabaseFunc)GetProcAddress(m_Handle, "GetReflectionDatabase");
	if (GetReflectionDatabase)
		m_ReflectionDB = GetReflectionDatabase();

	// Ask the DLL to register and interface implementations it has
	typedef void (*AddReflectionImplsFunc)(Module*);
	AddReflectionImplsFunc AddReflectionImpls = (AddReflectionImplsFunc)GetProcAddress(m_Handle, "AddReflectionImpls");
	if (AddReflectionImpls)
		AddReflectionImpls(this);

	return true;
}


void clutl::Module::SetInterfaceImpl(clcpp::Type* iface_type, const clcpp::Type* impl_type)
{
	clcpp::internal::Assert(m_HostReflectionDB != 0);
	clcpp::internal::Assert(m_ReflectionDB != 0);

	// Get non-const access to the interface class primitive
	clcpp::internal::Assert(iface_type != 0);
	clcpp::Class* iface_class = const_cast<clcpp::Class*>(iface_type->AsClass());

	// Get read access to the implementation class primitive
	clcpp::internal::Assert(impl_type != 0);
	const clcpp::Class* impl_class = impl_type->AsClass();

	// Copy all information required to construct an implementation object
	// Note that implementation details, such as list of fields, are excluded
	iface_class->size = impl_class->size;
	iface_class->constructor = impl_class->constructor;
	iface_class->destructor = impl_class->destructor;
}