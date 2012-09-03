
//
// ===============================================================================
// clReflect, Objects.h - A simple object model using the reflection API
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

#pragma once


#include <clcpp/clcpp.h>


//
// This is an example object management API that you can use, ignore or base your own
// designs upon.
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
	struct clcpp_attr(reflect_part, custom_flag = 0x10000000, custom_flag_inherit) Object
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
			return type == clcpp::GetType<TYPE>() ? (TYPE*)this : 0;
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
	// Create an object of the given type by allocating and constructing it.
	// This function has 3 possible modes of operation, based on which parameters you specify:
	//
	//    1. Create an anonymous object.
	//    2. Create a named object.
	//    3. Create a named object that is also tracked in an object group.
	//
	Object* CreateObject(const clcpp::Type* type, unsigned int unique_id = 0, ObjectGroup* object_group = 0);

	void DestroyObject(const Object* object);


	//
	// Hash table based storage of collections of objects.
	// The ObjectGroup is an object itself, allowing groups to be nested within other groups.
	//
	class clcpp_attr(reflect_part, custom_flag = 0x20000000, custom_flag_inherit) ObjectGroup : public Object
	{
	public:
		ObjectGroup();
		~ObjectGroup();

		// Find a created object by unique ID
		Object* FindObject(unsigned int unique_id) const;
		Object* FindObjectSearchParents(unsigned int unique_id) const;
		Object* FindObjectRelative(unsigned int* unique_ids, unsigned int nb_ids) const;

		friend Object* clutl::CreateObject(const clcpp::Type*, unsigned int, ObjectGroup*);
		friend void clutl::DestroyObject(const Object*);

	private:
		struct HashEntry;

		void AddObject(Object* object);
		void RemoveObject(const Object* object);
		void AddHashEntry(Object* object);
		void RemoveHashEntry(unsigned int hash);
		void Resize(bool increase);

		// An open-addressed hash table with linear probing - good cache behaviour for storing
		// hashes of pointers that may suffer from clustering.
		unsigned int m_MaxNbObjects;
		unsigned int m_NbObjects;
		unsigned int m_NbOccupiedEntries;
		HashEntry* m_NamedObjects;

		friend class ObjectIterator;
		friend class ObjectDatabase;
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
			DestroyObject(object);
			object = 0;
		}
	}
}


//
// Helpers for creating typed objects.
// Exposed publically as Koenig lookup doesn't apply to template parameters.
//
template <typename TYPE>
inline TYPE* New()
{
	return static_cast<TYPE*>(clutl::CreateObject(clcpp::GetType<TYPE>()));
}
template <typename TYPE>
inline TYPE* New(clutl::ObjectGroup* group, unsigned int unique_id = 0)
{
	return static_cast<TYPE*>(clutl::CreateObject(clcpp::GetType<TYPE>(), unique_id, group));
}
