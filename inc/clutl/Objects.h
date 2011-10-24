
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

	struct NamedObject : public Object
	{
		// TODO: Is it wise to use the same name type?
		clcpp::Name name;
	};


	class ObjectDatabase
	{
	public:
		ObjectDatabase(unsigned int max_nb_objects);
		~ObjectDatabase();

		// Create an object of a type that derives from clutl::Object and assign its type pointer
		template <typename TYPE> TYPE* CreateObject(const clcpp::Database& reflection_db, unsigned int type_hash)
		{
			const clcpp::Type* type = 0;
			TYPE* object = (TYPE*)CreateObject(reflection_db, type_hash, type);
			if (object)
				object->type = type;
			return object;
		}
		
		template <typename TYPE> TYPE* CreateObject(const clcpp::Database& reflection_db)
		{
			return CreateObject<TYPE>(reflection_db, clcpp::GetTypeNameHash<TYPE>());
		}

		// Destroy an object of a type that derives from clutl::Object, taking its type from the object
		template <typename TYPE> void DestroyObject(TYPE* object)
		{
			DestroyObject(object, object->type);
		}


		template <typename TYPE> TYPE* CreateNamedObject(const clcpp::Database& reflection_db, unsigned int type_hash, const char* name_text)
		{
			const clcpp::Type* type = 0;
			clcpp::Name name;
			TYPE* object = (TYPE*)CreateNamedObject(reflection_db, type_hash, name_text, type, name);
			if (object)
			{
				object->type = type;
				object->name = name;
			}
			return object;
		}

		template <typename TYPE> TYPE* CreateNamedObject(const clcpp::Database& reflection_db, const char* name_text)
		{
			return CreateObject(reflection_db, clcpp::GetTypeNameHash<TYPE>(), name_text);
		}

		template <typename TYPE> void DestroyNamedObject(TYPE* object)
		{
			DestroyNamedObject(object, object->type, object->name.hash);
		}

		//
		// Create and destroy objects of a given type name. After successful creation, the type pointer is returned
		// and can be stored in the object or used in another fashion. It must be used at a later date to destroy
		// the object.
		//
		void* CreateObject(const clcpp::Database& reflection_db, unsigned int type_hash, const clcpp::Type*& type);
		void DestroyObject(void* object, const clcpp::Type* object_type);

		// Create an object of the given type name and object name; a pointer to the type is returned
		void* CreateNamedObject(const clcpp::Database& reflection_db, unsigned int type_hash, const char* name_text, const clcpp::Type*& type, clcpp::Name& name);
		void DestroyNamedObject(void* object, const clcpp::Type* object_type, unsigned int name_hash);

	private:
		struct HashEntry
		{
			HashEntry() : object(0) { }
			clcpp::Name name;
			void* object;
		};

		HashEntry* FindHashEntry(unsigned int hash_index, unsigned int hash);

		// An open-addressed hash table with linear probing - good cache behaviour for storing
		// hashes of pointers that may suffer from clustering.
		unsigned int m_MaxNbObjects;
		HashEntry* m_NamedObjects;
	};
}