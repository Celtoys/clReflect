
#pragma once


#include <clcpp/clcpp.h>


// Only want to reflect the Object class so that derivers will reflect
// The type parameter is not interesting here
clcpp_reflect_part(clutl::Object)


namespace clutl
{
	struct Object
	{
		const clcpp::Type* type;
	};


	// TODO: Reflect this and walk its object list so that
	class ObjectDatabase
	{
	public:
		ObjectDatabase();
		~ObjectDatabase();


		// Create an object of the given type name and assign its type pointer
		template <typename TYPE> TYPE* CreateObject(const clcpp::Database& reflection_db, unsigned int type_hash)
		{
			const clcpp::Type* type = 0;
			TYPE* object = (TYPE*)CreateObject(reflection_db, type_hash, type);
			if (object)
				object->type = type;
			return object;
		}

		// Destroy an object, taking its type from the object
		template <typename TYPE> void DestroyObject(TYPE* object)
		{
			clcpp::internal::Assert(object != 0);
			DestroyObject(object, object->type);
		}

		// Create an object of the given type name; a pointer to the type is returned
		void* CreateObject(const clcpp::Database& reflection_db, unsigned int type_hash, const clcpp::Type*& type);

		// Destroy an object of the given type
		void DestroyObject(void* object, const clcpp::Type* object_type);
	};
}