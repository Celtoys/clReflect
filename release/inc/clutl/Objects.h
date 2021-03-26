
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
clcpp_reflect_part(clobj)
namespace clobj
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
	struct CLCPP_API clcpp_attr(reflect_part, custom_flag = 0x10000000, custom_flag_inherit) Object
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
		virtual ~Object();

		template <typename TYPE>
		TYPE* Cast()
		{
			return type == clcpp::GetType<TYPE>() ? (TYPE*)this : 0;
		}
		template <typename TYPE>
		const TYPE* Cast() const
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
	CLCPP_API Object* CreateObject(const clcpp::Type* type, unsigned int unique_id = 0, ObjectGroup* object_group = 0);

	CLCPP_API void DestroyObject(const Object* object);


	//
	// Hash table based storage of collections of objects.
	// The ObjectGroup is an object itself, allowing groups to be nested within other groups.
	//
	class CLCPP_API clcpp_attr(reflect_part, custom_flag = 0x20000000, custom_flag_inherit) ObjectGroup : public Object
	{
	public:
		ObjectGroup();
		~ObjectGroup();

		// Find a created object by unique ID
		Object* FindObject(unsigned int unique_id) const;
		Object* FindObjectSearchParents(unsigned int unique_id) const;
		Object* FindObjectRelative(unsigned int* unique_ids, unsigned int nb_ids) const;

		// For manual construction of objects with explicit specification of construction parameters
		// Object type and ID must be correctly setup before calling this
		void AddObject(Object* object);
		void RemoveObject(Object* object);

	private:
		struct HashEntry;

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
	// Object iterator type
	//
	enum IteratorType
	{
		IteratorSingle,
		IteratorRecursive,
	};


	//
	// Iterator for visiting all created objects in an object group.
	// The iterator is invalidated if objects are added/removed from the group.
	//
	class CLCPP_API ObjectIterator
	{
	public:
		ObjectIterator(const ObjectGroup* object_group, IteratorType type = IteratorSingle);
		~ObjectIterator();

		// Get the current object under iteration
		Object* GetObject() const;

		// Move onto the next object in the database
		void MoveNext();

		// Is the iterator still valid? Returns false after there are no more objects
		// left to iterate.
		bool IsValid() const;

	private:
		void PushGroup(const ObjectGroup* object_group);
		const ObjectGroup* PopGroup();
		void ScanForEntry();

		IteratorType m_Type;

		// On-demand allocated group stack for recursive iteration
		const ObjectGroup** m_GroupsToScan;
		unsigned int m_NbGroupsToScan;
		unsigned int m_GroupsCapacity;

		// Current group/entry under iteration
		const ObjectGroup* m_ObjectGroup;
		unsigned int m_Position;
	};


	// Disable clang warning about rvalue references being a C++11 extension
	#ifdef __clang__
	#pragma clang push
	#pragma clang diagnostic ignored "-Wc++11-extensions"
	#endif


	//
	// Use this to create instances of types that derive from Object. It does 4 things:
	//
	//    * Automatically assigns the object type after construction.
	//    * Sets the object unique ID after construction.
	//    * Adds the object to a group after construction.
	//    * Optionally perfectly forwards parameters onto the constructor of the type.
	//
	// Use cases:
	//
	//    // Create Type with no name and no group
	//    Type* o = clobj::New<Type>();
	//
	//    // Create Type with specified unique ID and group
	//    // Group pointer is optional, allowing the object to just be named
	//    Type* o = clobj::New<Type>(1234, group);
	//
	//    // Same as above, but now forwarding parameters onto constructor
	//    Type* o = clobj::New<Type>()(a, b, c);
	//    Type* o = clobj::New<Type>(1234, group)(a, b, c);
	//
	// It achieves this without being intrusive to the implementation in the following ways:
	//
	//    * Getting the type could be achieved by adding a virtual function to each class definition.
	//    * Setting the type in the constructor requires it to be passed down from the top, with
	//      mid-derivers having to forward them on.
	//    * Setting of ID and group could be manual function calls after creation which is tedious for
	//      the user.
	//    * Setting of ID and group could be implemented in the constructor of all derivers. This is not
	//      only tedious for the implementation but becomes harder in the face of name/group permutations.
	//
	template <typename TYPE>
	class New
	{
	public:
		// No unique ID and no group
		New()
			: m_UniqueID(0)
			, m_Group(0)
		{
		}

		// Assign a unique ID and optionally add to a group
		New(unsigned int unique_id, ObjectGroup* group = 0)
			: m_UniqueID(unique_id)
			, m_Group(group)
		{
		}

		// Create object with the default constructor
		// Casts this New object directly to the object created
		operator TYPE* () const
		{
			TYPE* object = new TYPE();
			return (TYPE*)SetObject(object, clcpp::GetType<TYPE>());
		}

		// Create object with constructor from forwarded parameters
		template <typename A0>
		TYPE* operator () (A0&& a0)
		{
			TYPE* object = new TYPE(static_cast<A0&&>(a0));
			return (TYPE*)SetObject(object, clcpp::GetType<TYPE>());
		}
		template <typename A0, typename A1>
		TYPE* operator () (A0&& a0, A1&& a1)
		{
			TYPE* object = new TYPE(static_cast<A0&&>(a0), static_cast<A1&&>(a1));
			return (TYPE*)SetObject(object, clcpp::GetType<TYPE>());
		}
		template <typename A0, typename A1, typename A2>
		TYPE* operator () (A0&& a0, A1&& a1, A2&& a2)
		{
			TYPE* object = new TYPE(static_cast<A0&&>(a0), static_cast<A1&&>(a1), static_cast<A2&&>(a2));
			return (TYPE*)SetObject(object, clcpp::GetType<TYPE>());
		}
		template <typename A0, typename A1, typename A2, typename A3>
		TYPE* operator () (A0&& a0, A1&& a1, A2&& a2, A3&& a3)
		{
			TYPE* object = new TYPE(static_cast<A0&&>(a0), static_cast<A1&&>(a1), static_cast<A2&&>(a2), static_cast<A3&&>(a3));
			return (TYPE*)SetObject(object, clcpp::GetType<TYPE>());
		}
		template <typename A0, typename A1, typename A2, typename A3, typename A4>
		TYPE* operator () (A0&& a0, A1&& a1, A2&& a2, A3&& a3, A4&& a4)
		{
			TYPE* object = new TYPE(static_cast<A0&&>(a0), static_cast<A1&&>(a1), static_cast<A2&&>(a2), static_cast<A3&&>(a3), static_cast<A4&&>(a4));
			return (TYPE*)SetObject(object, clcpp::GetType<TYPE>());
		}
		template <typename A0, typename A1, typename A2, typename A3, typename A4, typename A5>
		TYPE* operator () (A0&& a0, A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5)
		{
			TYPE* object = new TYPE(static_cast<A0&&>(a0), static_cast<A1&&>(a1), static_cast<A2&&>(a2), static_cast<A3&&>(a3), static_cast<A4&&>(a4), static_cast<A5&&>(a5));
			return (TYPE*)SetObject(object, clcpp::GetType<TYPE>());
		}
		template <typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
		TYPE* operator () (A0&& a0, A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, A6&& a6)
		{
			TYPE* object = new TYPE(static_cast<A0&&>(a0), static_cast<A1&&>(a1), static_cast<A2&&>(a2), static_cast<A3&&>(a3), static_cast<A4&&>(a4), static_cast<A5&&>(a5), static_cast<A6>(a6));
			return (TYPE*)SetObject(object, clcpp::GetType<TYPE>());
		}

	private:
		Object* SetObject(Object* object, const clcpp::Type* type) const
		{
			// Pass type with hope that the compiler generates smaller code as a result
			object->type = type;

			// Set the rest of the object from properties passed to constructor
			object->unique_id = m_UniqueID;
			object->object_group = m_Group;

			// Add to any object group
			if (m_Group != 0)
				m_Group->AddObject(object);

			return object;
		}

		unsigned int m_UniqueID;
		ObjectGroup* m_Group;
	};


	#ifdef __clang__
	#pragma clang push
	#endif
}
