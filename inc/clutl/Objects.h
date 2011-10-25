
#pragma once


#include <clcpp/clcpp.h>


// Only want to reflect the Object/NamedObject classes so that derivers will reflect
// The type parameter is not interesting here
clcpp_reflect_part(clutl::Object)
clcpp_reflect_part(clutl::NamedObject)


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

		//
		// Template helpers for CreateObject/DestroyObject for types that derive from clutl::Object. After
		// successful object creation, the type pointer is correctly assigned and is used later to
		// destroy any created objects.
		//
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
		template <typename TYPE> void DestroyObject(TYPE* object)
		{
			clcpp::internal::Assert(object != 0);
			DestroyObject(object, object->type);
		}


		//
		// Template helpers for CreateNamedObject/DestroyNamedObject for types that derive from
		// clutl::NamedObject. After successful object creation, the type pointer and name is correctly
		// assigned and is used later to destroy any created objects.
		//
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
			return CreateNamedObject<TYPE>(reflection_db, clcpp::GetTypeNameHash<TYPE>(), name_text);
		}
		template <typename TYPE> void DestroyNamedObject(TYPE* object)
		{
			clcpp::internal::Assert(object != 0);
			DestroyNamedObject(object, object->type, object->name.hash);
		}

		//
		// Create and destroy objects of a given type name. After successful creation, the type pointer is returned
		// and can be stored in the object or used in another fashion. It must be used at a later date to destroy
		// the object.
		//
		void* CreateObject(const clcpp::Database& reflection_db, unsigned int type_hash, const clcpp::Type*& type);
		void DestroyObject(void* object, const clcpp::Type* object_type);

		//
		// Create and destroy objects of the given type name and object name. After successful creation, the type
		// pointer and persistent name is returned. These must be used later to destroy the object. The object
		// database tracks all named objects created this way.
		//
		void* CreateNamedObject(const clcpp::Database& reflection_db, unsigned int type_hash, const char* name_text, const clcpp::Type*& type, clcpp::Name& name);
		void DestroyNamedObject(void* object, const clcpp::Type* object_type, unsigned int name_hash);

		void* FindNamedObject(unsigned int name_hash) const;

	private:
		struct HashEntry
		{
			HashEntry() : object(0) { }
			clcpp::Name name;
			void* object;
		};

		const HashEntry* FindHashEntry(unsigned int hash_index, unsigned int hash) const;

		// An open-addressed hash table with linear probing - good cache behaviour for storing
		// hashes of pointers that may suffer from clustering.
		unsigned int m_MaxNbObjects;
		HashEntry* m_NamedObjects;

		friend class ObjectIterator;
	};


	//
	// Iterator for visiting all created objects in an object database.
	//
	class ObjectIterator
	{
	public:
		ObjectIterator(const ObjectDatabase& object_db);

		// Get the current object or object name under iteration
		void* GetObject() const;
		clcpp::Name GetObjectName() const;

		// Move onto the next object in the database
		void MoveNext();

		// Is the iterator still valid? Returns false after there are no more objects
		// left to iterate.
		bool IsValid() const;

	private:
		void ScanForEntry();

		const ObjectDatabase& m_ObjectDB;
		unsigned int m_Position;
	};
}