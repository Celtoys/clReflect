
// TODO: Lots of stuff happening in here that needs logging

#include <clutl/Objects.h>
#include <clcpp/FunctionCall.h>


struct clutl::ObjectGroup::HashEntry
{
	HashEntry() : hash(0), object(0) { }
	unsigned int hash;
	Object* object;
};



void clutl::Object::Delete() const
{
	if (object_group)
		object_group->DestroyObject(this);
}


clutl::ObjectGroup::ObjectGroup()
	: m_ReflectionDB(0)
	, m_MaxNbObjects(8)
	, m_NbObjects(0)
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


clutl::ObjectGroup* clutl::ObjectGroup::CreateObjectGroup(unsigned int unique_id)
{
	return (ObjectGroup*)CreateObject(clcpp::GetTypeNameHash<ObjectGroup>(), unique_id);
}


clutl::Object* clutl::ObjectGroup::CreateObject(unsigned int type_hash)
{
	// Can the type be located?
	const clcpp::Type* type = m_ReflectionDB->GetType(type_hash);
	if (type == 0)
		return 0;

	// Can only create class objects
	if (type->kind != clcpp::Primitive::KIND_CLASS)
		return 0;
	const clcpp::Class* class_type = type->AsClass();

	// The object group has no registered constructor so construct manually
	// if it comes through
	Object* object = 0;
	if (type_hash == clcpp::GetTypeNameHash<ObjectGroup>())
	{
		object = new ObjectGroup();
	}
	else
	{
		// Need a constructor to new and a destructor to delete at a later point
		if (class_type->constructor == 0 || class_type->destructor == 0)
			return 0;
		object = (Object*)new char[type->size];
		CallFunction(class_type->constructor, object);
	}

	// Construct the object and pass on any reflection DB pointer to derivers
	// of the object group type
	object->object_group = this;
	object->type = type;
	if (class_type->flag_attributes & FLAG_ATTR_IS_OBJECT_GROUP)
		((ObjectGroup*)object)->m_ReflectionDB = m_ReflectionDB;

	return object;
}


clutl::Object* clutl::ObjectGroup::CreateObject(unsigned int type_hash, unsigned int unique_id)
{
	// Create the object
	Object* object = CreateObject(type_hash);
	if (object == 0)
		return 0;

	// Add to the hash table if there is a unique ID assigned
	if (unique_id != 0)
	{
		object->unique_id = unique_id;
		AddHashEntry(object);
	}

	return object;
}


void clutl::ObjectGroup::DestroyObject(const Object* object)
{
	// Remove from the hash table if it's named
	if (object->unique_id != 0)
		RemoveHashEntry(object);

	// These represent fatal code errors
	clcpp::internal::Assert(object != 0);
	clcpp::internal::Assert(object->type != 0);

	if (object->type->name.hash == clcpp::GetTypeNameHash<ObjectGroup>())
	{
		// ObjecGroup class does not have a registered destructor
		delete (ObjectGroup*)object;
	}

	else
	{
		// Call the destructor and release the memory
		const clcpp::Class* class_type = object->type->AsClass();
		clcpp::internal::Assert(class_type->destructor != 0);
		CallFunction(class_type->destructor, object);
		delete [] (char*)object;
	}
}


clutl::Object* clutl::ObjectGroup::FindObject(unsigned int unique_id) const
{
	// Search up through the object group hierarchy
	const ObjectGroup* group = this;
	Object* object = 0;
	while (object == 0 && group != 0)
	{
		HashEntry* named_objects = group->m_NamedObjects;

		// Linear probe from the natural hash location for matching hash
		const unsigned int index_mask = group->m_MaxNbObjects - 1;
		unsigned int index = unique_id & index_mask;
		while (named_objects[index].hash)
		{
			// Ensure dummy objects are skipped
			if (named_objects[index].hash == unique_id &&
				named_objects[index].object != 0)
				break;

			index = (index + 1) & index_mask;
		}

		// Get the object here
		HashEntry& he = group->m_NamedObjects[index];
		object = he.object;
		group = group->object_group;
	}

	return object;
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

	// Resize when load factor is greather than 2/3
	if (m_NbObjects > (m_MaxNbObjects * 2) / 3)
		Resize();
}


void clutl::ObjectGroup::RemoveHashEntry(const Object* object)
{
	// Linear probe from the natural hash location for matching hash
	unsigned int hash = object->unique_id;
	const unsigned int index_mask = m_MaxNbObjects - 1;
	unsigned int index = hash & index_mask;
	while (m_NamedObjects[index].hash && m_NamedObjects[index].hash != hash)
		index = (index + 1) & index_mask;

	// Leave the has key in-place, clearing the object pointer, marking the object as a dummy object
	HashEntry& he = m_NamedObjects[index];
	he.object = 0;
	m_NbObjects--;
}


void clutl::ObjectGroup::Resize()
{
	// Make a bigger, empty table
	unsigned int old_max_nb_objects = m_MaxNbObjects;
	HashEntry* old_named_objects = m_NamedObjects;
	if (m_MaxNbObjects < 8192 * 4)
		m_MaxNbObjects *= 4;
	else
		m_MaxNbObjects *= 2;
	m_NamedObjects = new HashEntry[m_MaxNbObjects];

	// Reinsert all objects into the new hash table
	m_NbObjects = 0;
	for (unsigned int i = 0; i < old_max_nb_objects; i++)
	{
		HashEntry& he = old_named_objects[i];
		if (he.object != 0)
			AddHashEntry(he.object);
	}

	delete [] old_named_objects;
}


clutl::ObjectDatabase::ObjectDatabase(const clcpp::Database* reflection_db)
	: m_RootGroup(0)
{
	m_RootGroup = new ObjectGroup();
	m_RootGroup->m_ReflectionDB = reflection_db;
}


clutl::ObjectDatabase::~ObjectDatabase()
{
	delete m_RootGroup;
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
