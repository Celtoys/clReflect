
#pragma once


#include <clcpp/clcpp.h>


// Only want to reflect the Object/NamedObject classes so that derivers will reflect
// The type parameter is not interesting here
clcpp_reflect_part(clutl::Object)
clcpp_reflect_part(clutl::NamedObject)


//
// This is an example object management API that you can use, ignore or base your own
// designs upon. It is required by the serialisation API in this library which would need
// to be branched/copied should you want to use your own object management API.
//
namespace clutl
{
	class ObjectDatabase;


	//
	// Base object class for objects that require runtime knowledge of their type
	//
	struct Object
	{
		// Default constructor
		Object()
			: object_db(0)
			, type(0)
		{
		}

		//
		// Make all deriving types carry a virtual function table. Since many use-cases may require
		// the use of virtual functions, this ensures safety and convenience.
		//
		// If Object did not carry a vftable and you derived from it with a type, X, that did carry
		// a vftable, casting a pointer between the two types would result in different pointer
		// addresses.
		//
		// If Object has a vftable then the address will always be consistent between casts. This
		// allows the Object Database to create objects by type name, cast them to an Object and
		// automatically assign the type pointer, without the use of templates and in the general
		// case.
		//
		virtual ~Object() { }

		// Object database that owns this object
		ObjectDatabase* object_db;

		// Type of the object
		const clcpp::Type* type;
	};


	//
	// Base object class for objects that are to be the root of any serialisation network
	// Doesn't inherit from Object so that it's impossible to mistakenly treat a NamedObject
	// as an Object.
	//
	struct NamedObject
	{
		// Default constructor
		NamedObject()
			: object_db(0)
			, type(0)
		{
		}

		// Carry a virtual function table, as described for Object
		virtual ~NamedObject() { }

		// Object database that owns this object
		ObjectDatabase* object_db;

		// Type of the object
		const clcpp::Type* type;

		// TODO: Is it wise to use the same name type?
		clcpp::Name name;
	};


	class ObjectDatabase
	{
	public:
		ObjectDatabase(const clcpp::Database* reflection_db, unsigned int max_nb_objects);
		~ObjectDatabase();

		// Template helpers for acquring the required typename and correctly casting during creation
		template <typename TYPE> TYPE* CreateObject()
		{
			return static_cast<TYPE*>(CreateObject(clcpp::GetTypeNameHash<TYPE>()));
		}
		template <typename TYPE> TYPE* CreateNamedObject(const char* name_text)
		{
			return static_cast<TYPE*>(CreateNamedObject(clcpp::GetTypeNameHash<TYPE>(), name_text));
		}

		// Create and destroy objects of a given type name
		Object* CreateObject(unsigned int type_hash);
		void DestroyObject(Object* object);

		// Create and destroy objects of the given type name and object name. Objects created with this method
		// are internally tracked and can be requested by name at a later point.
		NamedObject* CreateNamedObject(unsigned int type_hash, const char* name_text);
		void DestroyNamedObject(NamedObject* object);

		NamedObject* FindNamedObject(unsigned int name_hash) const;

	private:
		struct HashEntry
		{
			HashEntry() : object(0) { }
			clcpp::Name name;
			NamedObject* object;
		};

		const HashEntry* FindHashEntry(unsigned int hash_index, unsigned int hash) const;

		const clcpp::Database* m_ReflectionDB;

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