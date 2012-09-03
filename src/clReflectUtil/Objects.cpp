
//
// ===============================================================================
// clReflect
// -------------------------------------------------------------------------------
// Copyright (c) 2011-2012 Don Williamson & clReflect Authors (see AUTHORS file)
// Released under MIT License (see LICENSE file)
// ===============================================================================
//

// TODO: Lots of stuff happening in here that needs logging

#include <clutl/Objects.h>


// Store this here, rather than using GetTypeNameHash so that this library
// can be used without generating an implementation of GetTypeNameHash.
static unsigned int g_ObjectGroupHash = clcpp::internal::HashNameString("clutl::ObjectGroup");


struct clutl::ObjectGroup::HashEntry
{
	HashEntry() : hash(0), object(0) { }
	unsigned int hash;
	Object* object;
};


clutl::Object* clutl::CreateObject(const clcpp::Type *type, unsigned int unique_id, ObjectGroup* object_group)
{
	if (type == 0)
		return 0;

	// Can only create class objects
	if (type->kind != clcpp::Primitive::KIND_CLASS)
		return 0;
	const clcpp::Class* class_type = type->AsClass();

	// The object group has no registered constructor so construct manually
	// if it comes through
	Object* object = 0;
	if (type->name.hash == g_ObjectGroupHash)
	{
		object = new ObjectGroup();
	}
	else
	{
		// Need a constructor to new and a destructor to delete at a later point
		if (class_type->constructor == 0 || class_type->destructor == 0)
			return 0;

		// Allocate and call the constructor
		object = (Object*)new char[type->size];
		typedef void (*CallFunc)(clutl::Object*);
		CallFunc call_func = (CallFunc)class_type->constructor->address;
		call_func(object);
	}

	// Construct the object and add to its object group
	object->type = type;
	object->unique_id = unique_id;
	if (object_group)
		object_group->AddObject(object);

	return object;
}


void clutl::DestroyObject(const Object* object)
{
	// These represent fatal code errors
	clcpp::internal::Assert(object != 0);
	clcpp::internal::Assert(object->type != 0);

	// Remove from any attached object group
	if (object->object_group != 0)
		object->object_group->RemoveObject(object);

	if (object->type->name.hash == g_ObjectGroupHash)
	{
		// ObjecGroup class does not have a registered destructor
		delete (ObjectGroup*)object;
	}

	else
	{
		// Call the destructor and release the memory
		const clcpp::Class* class_type = object->type->AsClass();
		clcpp::internal::Assert(class_type->destructor != 0);
		typedef void (*CallFunc)(const clutl::Object*);
		CallFunc call_func = (CallFunc)class_type->destructor->address;
		call_func(object);
		delete [] (char*)object;
	}
}




clutl::ObjectGroup::ObjectGroup()
	: m_MaxNbObjects(8)
	, m_NbObjects(0)
	, m_NbOccupiedEntries(0)
	, m_NamedObjects(0)
{
	// Allocate the hash table
	m_NamedObjects = new HashEntry[m_MaxNbObjects];
}


clutl::ObjectGroup::~ObjectGroup()
{
	if (m_NamedObjects != 0)
		delete [] m_NamedObjects;
}


void clutl::ObjectGroup::AddObject(Object* object)
{
	object->object_group = this;
	if (object->unique_id != 0)
		AddHashEntry(object);
}


void clutl::ObjectGroup::RemoveObject(const Object* object)
{
	// Remove from the hash table if it's named
	if (object->unique_id != 0)
		RemoveHashEntry(object->unique_id);
}


clutl::Object* clutl::ObjectGroup::FindObject(unsigned int unique_id) const
{
	// Linear probe from the natural hash location for matching hash
	const unsigned int index_mask = m_MaxNbObjects - 1;
	unsigned int index = unique_id & index_mask;
	while (m_NamedObjects[index].hash)
	{
		// Ensure dummy objects are skipped
		if (m_NamedObjects[index].hash == unique_id &&
			m_NamedObjects[index].object != 0)
			break;

		index = (index + 1) & index_mask;
	}

	// Get the object here
	HashEntry& he = m_NamedObjects[index];
	return he.object;
}


clutl::Object* clutl::ObjectGroup::FindObjectSearchParents(unsigned int unique_id) const
{
	// Search up through the object group hierarchy
	const ObjectGroup* group = this;
	Object* object = 0;
	while (group != 0)
	{
		object = group->FindObject(unique_id);
		if (object != 0)
			break;

		group = group->object_group;
	}

	return object;
}


clutl::Object* clutl::ObjectGroup::FindObjectRelative(unsigned int* unique_ids, unsigned int nb_ids) const
{
	// Locate the containing object group
	const ObjectGroup* object_group = this;
	while (nb_ids - 1 > 0)
	{
		Object* object = FindObject(*unique_ids++);
		if (object == 0)
			return 0;

		// Ensure this is an object group
		if (object->type->kind != clcpp::Primitive::KIND_CLASS)
			return 0;
		const clcpp::Class* class_type = (clcpp::Class*)object->type;
		if (!(class_type->flag_attributes & FLAG_ATTR_IS_OBJECT_GROUP))
			return 0;

		object_group = (ObjectGroup*)object;
		nb_ids--;
	}

	return object_group->FindObject(*unique_ids);
}


void clutl::ObjectGroup::AddHashEntry(Object* object)
{
	// Linear probe from the natural hash location for a free slot, reusing any dummy slots
	unsigned int hash = object->unique_id;
	const unsigned int index_mask = m_MaxNbObjects - 1;
	unsigned int index = hash & index_mask;
	while (m_NamedObjects[index].hash && m_NamedObjects[index].object != 0)
		index = (index + 1) & index_mask;

	// Add to the table
	HashEntry& he = m_NamedObjects[index];
	he.hash = hash;
	he.object = object;
	m_NbObjects++;
	m_NbOccupiedEntries++;

	// Resize when load factor is greather than 2/3
	if (m_NbObjects > (m_MaxNbObjects * 2) / 3)
		Resize(true);
	
	// Or flush dummy objects so that there is always at least on empty slot
	// This is required for the FindObject loop to terminate when an object can't be find
	else if (m_NbOccupiedEntries == m_MaxNbObjects)
		Resize(false);
}


void clutl::ObjectGroup::RemoveHashEntry(unsigned int hash)
{
	// Linear probe from the natural hash location for matching hash
	const unsigned int index_mask = m_MaxNbObjects - 1;
	unsigned int index = hash & index_mask;
	while (m_NamedObjects[index].hash && m_NamedObjects[index].hash != hash)
		index = (index + 1) & index_mask;

	// Leave the has key in-place, clearing the object pointer, marking the object as a dummy object
	HashEntry& he = m_NamedObjects[index];
	he.object = 0;
	m_NbObjects--;
}


void clutl::ObjectGroup::Resize(bool increase)
{
	// Backup existing table
	unsigned int old_max_nb_objects = m_MaxNbObjects;
	HashEntry* old_named_objects = m_NamedObjects;

	// Either make the table bigger or leave it the same size to flush all dummy objects
	if (increase)
	{
		if (m_MaxNbObjects < 8192 * 4)
			m_MaxNbObjects *= 4;
		else
			m_MaxNbObjects *= 2;
	}
	m_NamedObjects = new HashEntry[m_MaxNbObjects];

	// Reinsert all objects into the new hash table
	m_NbObjects = 0;
	m_NbOccupiedEntries = 0;
	for (unsigned int i = 0; i < old_max_nb_objects; i++)
	{
		HashEntry& he = old_named_objects[i];
		if (he.object != 0)
			AddHashEntry(he.object);
	}

	delete [] old_named_objects;
}


clutl::ObjectIterator::ObjectIterator(const ObjectGroup* object_group)
	: m_ObjectGroup(object_group)
	, m_Position(0)
{
	// Search for the first non-empty slot
	ScanForEntry();
}


clutl::Object* clutl::ObjectIterator::GetObject() const
{
	clcpp::internal::Assert(IsValid());
	return m_ObjectGroup->m_NamedObjects[m_Position].object;
}


void clutl::ObjectIterator::MoveNext()
{
	m_Position++;
	ScanForEntry();
}


bool clutl::ObjectIterator::IsValid() const
{
	return m_Position < m_ObjectGroup->m_MaxNbObjects;
}


void clutl::ObjectIterator::ScanForEntry()
{
	// Search for the next non-empty slot
	while (m_Position < m_ObjectGroup->m_MaxNbObjects &&
		m_ObjectGroup->m_NamedObjects[m_Position].object == 0)
		m_Position++;
}
