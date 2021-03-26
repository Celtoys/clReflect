
//
// ===============================================================================
// clReflect, Module.h - Interface/implementation support
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once


#include <clcpp/clcpp.h>


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
	class CLCPP_API Module
	{
	public:
		Module();
		~Module();

		//
		// Load the module and optionally get its reflection database
		//
		// The module can expose a function with the following signature to return its
		// reflection database:
		//
		//    extern "C" clcpp::Database* GetReflectionDatabase();
		//
		bool Load(clcpp::Database* host_db, const char* filename);

		void* GetFunction(const char* name) const;
		
		const clcpp::Database* GetReflectionDB() const { return m_ReflectionDB; }

	private:
		// Platform-specific module handle
		void* m_Handle;

		// The loading modules database
		clcpp::Database* m_HostReflectionDB;

		// Module database
		const clcpp::Database* m_ReflectionDB;
	};
}
