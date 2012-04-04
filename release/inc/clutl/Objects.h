
#pragma once


#include <clcpp/clcpp.h>


//
// This is an example object management API that you can use, ignore or base your own
// designs upon. It is required by the serialisation API in this library which would need
// to be branched/copied should you want to use your own object management API.
//
clcpp_reflect_part(clutl)
namespace clutl
{
	//
	// Custom flag attributes for quickly determining if a type inherits from Object or ObjectGroup
	//
	enum
	{
		FLAG_ATTR_IS_OBJECT			= 0x10000000,
		FLAG_ATTR_IS_OBJECT_GROUP	= 0x20000000,
	};


	//
	// Base object class for objects that require runtime knowledge of their type
	//
	clcpp_attr(reflect_part, custom_flag = 0x10000000, custom_flag_inherit)
	struct Object
	{
		// Default constructor
		Object()
			: type(0)
			, unique_id(0)
			, object_group(0)
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

		template <typename TYPE>
		TYPE* Cast()
		{
			if (type == clcpp::GetType<TYPE>())
				return (TYPE*)this;
			return 0;
		}

		// Type of the object
		const clcpp::Type* type;

		// Unique ID for storing the object within an object group and quickly retrieving it
		// If this is zero, the object is anonymous and not tracked
		unsigned int unique_id;

		// Object group that owns this object
		class ObjectGroup* object_group;
	};


	//
	// Hash table based storage of collections of objects.
	// The ObjectGroup is an object itself, allowing groups to be nested within other groups.
	//
	clcpp_attr(reflect_part, custom_flag = 0x20000000, custom_flag_inherit)
	class ObjectGroup : public Object
	{
	public:
		ObjectGroup();
		~ObjectGroup();

		// Create a nested group within this one
		ObjectGroup* CreateObjectGroup(unsigned int unique_id);

		// Create an anonymous object which doesn't get tracked by the database
		Object* CreateObject(unsigned int type_hash);

		// Create a named object that is internally tracked by name and can be found at a later point
		Object* CreateObject(unsigned int type_hash, unsigned int unique_id);

		// Destroy named/anonymous object or an object group
		void DestroyObject(const Object* object);

		// Find a created object by unique ID
		Object* FindObject(unsigned int unique_id) const;

		// Set whether searches for objects in this group are allowed to walk up
		// the hierarchy looking for matches
		// TEMPORARY SOLUTION for multi-threaded access and locking of various object groups
		// This concept of tree-based access doesn't work well with MPP so will be replaced with something simpler
		// and more adaptable.
		void AllowFindInParent(bool allow) { m_AllowFindInParent = allow; }

		const clcpp::Database* GetReflectionDB() const { return m_ReflectionDB; }

	private:
		struct HashEntry;

		void AddHashEntry(Object* object);
		void RemoveHashEntry(const Object* object);
		void Resize(bool increase);

		// Reflection database to use for type access
		const clcpp::Database* m_ReflectionDB;

		// An open-addressed hash table with linear probing - good cache behaviour for storing
		// hashes of pointers that may suffer from clustering.
		unsigned int m_MaxNbObjects;
		unsigned int m_NbObjects;
		unsigned int m_NbOccupiedEntries;
		HashEntry* m_NamedObjects;

		// Allow FindObject to recurse into the parent object group?
		bool m_AllowFindInParent;

		friend class ObjectIterator;
		friend class ObjectDatabase;
	};


	//
	// The main object database, currently just a holder for a root object group.
	// May be extended at a later date to do scoped named lookup.
	//
	class ObjectDatabase
	{
	public:
		ObjectDatabase(const clcpp::Database* reflection_db);
		~ObjectDatabase();

		ObjectGroup* GetRootGroup() const { return m_RootGroup; }

	private:
		ObjectGroup* m_RootGroup;
	};


	//
	// Iterator for visiting all created objects in an object group.
	// The iterator is invalidated if objects are added/removed from the group.
	//
	class ObjectIterator
	{
	public:
		ObjectIterator(const ObjectGroup* object_group);

		// Get the current object under iteration
		Object* GetObject() const;

		// Move onto the next object in the database
		void MoveNext();

		// Is the iterator still valid? Returns false after there are no more objects
		// left to iterate.
		bool IsValid() const;

	private:
		void ScanForEntry();

		const ObjectGroup* m_ObjectGroup;
		unsigned int m_Position;
	};


	//
	// Helper for safely deleting an object and nulling the pointer.
	// Needs to be templated so that a pointer reference can be passed.
	//
	template <typename TYPE>
	inline void Delete(TYPE*& object)
	{
		if (object != 0)
		{
			clcpp::internal::Assert(object->object_group != 0);
			object->object_group->DestroyObject(object);
			object = 0;
		}
	}
}


//
// Helpers for creating typed objects in object groups.
// Exposed publically as Koenig lookup doesn't apply to template parameters.
//
template <typename TYPE>
inline TYPE* New(clutl::ObjectGroup* group)
{
	return static_cast<TYPE*>(group->CreateObject(clcpp::GetTypeNameHash<TYPE>()));
}
template <typename TYPE>
inline TYPE* New(clutl::ObjectGroup* group, unsigned int unique_id)
{
	return static_cast<TYPE*>(group->CreateObject(clcpp::GetTypeNameHash<TYPE>(), unique_id));
}
