
#pragma once


namespace clcpp
{
	class Database;
	struct Type;
}


namespace clutl
{
	//
	// Represents a DLL on Windows, the only supported platform, currently
	//
	class Module
	{
	public:
		Module();
		~Module();

		//
		// Load the module, get its reflection database and register any interface implementations
		//
		// The module can optionally expose a function with the following signature to return its
		// reflection database:
		//
		//    extern "C" clcpp::Database* GetReflectionDatabase();
		//
		// If you have an interface type in the host program that has an implementation in the module,
		// expose a function with the following signature:
		//
		//    extern "C" void AddReflectionImpls(clutl::Module*);
		//
		// This will be called after the module load and you can use SetInterfaceImpl to register any
		// interface iimplementations.
		//
		bool Load(clcpp::Database* host_db, const char* filename);

		//
		// Registers an interface implementation from with AddReflectionImpls. Note that none of the
		// types need to be in-scope when you call this; they only need to be forward-declared.
		//
		template <typename IFACE_TYPE, typename IMPL_TYPE>
		void SetInterfaceImpl()
		{
			clcpp::internal::Assert(m_HostReflectionDB != 0);
			clcpp::internal::Assert(m_ReflectionDB != 0);

			// Cast away const-ness so the interface can alias its implementation
			clcpp::Type* iface_type = const_cast<clcpp::Type*>(m_HostReflectionDB->GetType(clcpp::GetTypeNameHash<IFACE_TYPE>()));
			const clcpp::Type* impl_type = m_ReflectionDB->GetType(clcpp::GetTypeNameHash<IMPL_TYPE>());
			SetInterfaceImpl(iface_type, impl_type);
		}

		const clcpp::Database* GetReflectionDB() { return m_ReflectionDB; }

	private:
		void SetInterfaceImpl(clcpp::Type* iface_type, const clcpp::Type* impl_type);

		// Platform-specific module handle
		void* m_Handle;

		// The loading modules database
		clcpp::Database* m_HostReflectionDB;

		// Module database
		const clcpp::Database* m_ReflectionDB;
	};
}