
// TODO: Lots of stuff happening in here that needs logging

#include <clutl/Objects.h>
#include <clcpp/FunctionCall.h>


// Explicit dependencies
// TODO: Some how remove the need for these or provide a means of locating them on the target platform
extern "C" void* __cdecl memcpy(void* dst, const void* src, unsigned int size);


struct clutl::ObjectGroup::HashEntry
{
	HashEntry() : hash(0), object(0) { }
	unsigned int hash;
	Object* object;
};



namespace
{
	unsigned int strlen(const char* str)
	{
		const char *end = str;
		while (*end++) ;
		return end - str - 1;
	}
}


void clutl::Object::Delete() const
{
	if (object_group)
		object_group->DestroyObject(this);
}


clutl::ObjectGroup::ObjectGroup(const clcpp::Database* reflection_db)
	: m_ReflectionDB(reflection_db)
	, m_MaxNbObjects(8)
	, m_NbObjects(0)
	, m_NamedObjects(0)
{
	clcpp::internal::Assert(m_ReflectionDB != 0);

	// Allocate the hash table
	m_NamedObjects = new HashEntry[m_MaxNbObjects];
}


clutl::ObjectGroup::~ObjectGroup()
{
	if (m_NamedObjects != 0)
		delete [] m_NamedObjects;
}


clutl::ObjectGroup* clutl::ObjectGroup::CreateObjectGroup(const char* name_text)
{
	// Can the object group type be located?
	const clcpp::Type* type = m_ReflectionDB->GetType(clcpp::GetTypeNameHash<ObjectGroup>());
	if (type == 0)
		return 0;

	// Manually new the object
	clutl::ObjectGroup* group = new clutl::ObjectGroup(m_ReflectionDB);
	group->object_group = this;
	group->type = type;

	// Construct the name
	int name_length = strlen(name_text);
	group->name.text = new char[name_length + 1];
	memcpy((void*)group->name.text, name_text, name_length + 1);

	// Add to the hash table
	group->name.hash = clcpp::internal::HashData(group->name.text, name_length);
	AddHashEntry(group);

	return group;
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

	// Need a constructor to new and a destructor to delete at a later point
	if (class_type->constructor == 0 || class_type->destructor == 0)
		return 0;

	// Allocate and construct the object
	Object* object = (Object*)new char[type->size];
	CallFunction(class_type->constructor, object);
	object->object_group = this;
	object->type = type;
	return object;
}


clutl::Object* clutl::ObjectGroup::CreateObject(unsigned int type_hash, const char* name_text)
{
	// Create the object
	Object* object = CreateObject(type_hash);
	if (object == 0)
		return 0;

	// Construct the name
	int name_length = strlen(name_text);
	object->name.text = new char[name_length + 1];
	memcpy((void*)object->name.text, name_text, name_length + 1);

	// Add to the hash table
	object->name.hash = clcpp::internal::HashData(object->name.text, name_length);
	AddHashEntry(object);

	return object;
}


void clutl::ObjectGroup::DestroyObject(const Object* object)
{
	// Remove from the hash table if it's named
	if (object->name.hash != 0)
	{
		RemoveHashEntry(object);

		// Release the name
		clcpp::internal::Assert(object->name.text != 0);
		delete [] object->name.text;
	}

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


clutl::Object* clutl::ObjectGroup::FindObject(unsigned int name_hash) const
{
	// Search up through the object group hierarchy
	const ObjectGroup* group = this;
	Object* object = 0;
	while (object == 0 && group != 0)
	{
		HashEntry* named_objects = group->m_NamedObjects;

		// Linear probe from the natural hash location for matching hash
		const unsigned int index_mask = group->m_MaxNbObjects - 1;
		unsigned int index = name_hash & index_mask;
		while (named_objects[index].hash)
		{
			// Ensure dummy objects are skipped
			if (named_objects[index].hash == name_hash &&
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
	unsigned int name_hash = object->name.hash;
	const unsigned int index_mask = m_MaxNbObjects - 1;
	unsigned int index = name_hash & index_mask;
	while (m_NamedObjects[index].hash && m_NamedObjects[index].object != 0)
		index = (index + 1) & index_mask;

	// Add to the table
	HashEntry& he = m_NamedObjects[index];
	he.hash = object->name.hash;
	he.object = object;
	m_NbObjects++;

	// Resize when load factor is greather than 2/3
	if (m_NbObjects > (m_MaxNbObjects * 2) / 3)
		Resize();
}


void clutl::ObjectGroup::RemoveHashEntry(const Object* object)
{
	// Linear probe from the natural hash location for matching hash
	unsigned int name_hash = object->name.hash;
	const unsigned int index_mask = m_MaxNbObjects - 1;
	unsigned int index = name_hash & index_mask;
	while (m_NamedObjects[index].hash && m_NamedObjects[index].hash != name_hash)
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
	m_RootGroup = new ObjectGroup(reflection_db);
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
