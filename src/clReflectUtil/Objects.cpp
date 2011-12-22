
// TODO: Lots of stuff happening in here that needs logging

#include <clutl/Objects.h>
#include <clcpp/FunctionCall.h>


// Explicit dependencies
// TODO: Some how remove the need for these or provide a means of locating them on the target platform
extern "C" void* __cdecl memcpy(void* dst, const void* src, unsigned int size);
extern "C" void* __cdecl memset(void* dst, int val, unsigned int size);



namespace
{
	// Table of primes, each around twice the size of those prior and as far as possible from the nearest pow2 numbers
	unsigned int g_HashTableSizes[] =
	{
		53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49167, 98317, 196613, 393241, 786433, 1572869
	};


	unsigned int strlen(const char* str)
	{
		const char *end = str;
		while (*end++) ;
		return end - str - 1;
	}
}


void clutl::Object::Delete() const
{
	if (object_db)
		object_db->DestroyObject(this);
}


clutl::ObjectDatabase::ObjectDatabase(const clcpp::Database* reflection_db, unsigned int max_nb_objects)
	: m_ReflectionDB(reflection_db)
	, m_MaxNbObjects(0)
	, m_NamedObjects(0)
{
	clcpp::internal::Assert(reflection_db != 0);

	// Round the max number of objects up to the nearest prime number in the table
	unsigned int prev_size = 0;
	for (unsigned int i = 0; i < sizeof(g_HashTableSizes) / sizeof(g_HashTableSizes[0]); i++)
	{
		if (max_nb_objects > prev_size && max_nb_objects <= g_HashTableSizes[i])
		{
			max_nb_objects = g_HashTableSizes[i];
			break;
		}
		prev_size = g_HashTableSizes[i];
	}

	// Allocate the hash table
	m_MaxNbObjects = max_nb_objects;
	m_NamedObjects = new HashEntry[m_MaxNbObjects];
	memset(m_NamedObjects, 0, m_MaxNbObjects * sizeof(HashEntry));
}


clutl::ObjectDatabase::~ObjectDatabase()
{
	if (m_NamedObjects != 0)
		delete [] m_NamedObjects;
}


clutl::Object* clutl::ObjectDatabase::CreateObject(unsigned int type_hash)
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
	object->object_db = this;
	object->type = type;
	return object;
}


clutl::Object* clutl::ObjectDatabase::CreateObject(unsigned int type_hash, const char* name_text)
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


void clutl::ObjectDatabase::DestroyObject(const Object* object)
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

	// Call the destructor and release the memory
	const clcpp::Class* class_type = object->type->AsClass();
	clcpp::internal::Assert(class_type->destructor != 0);
	CallFunction(class_type->destructor, object);
	delete [] (char*)object;
}


clutl::Object* clutl::ObjectDatabase::FindObject(unsigned int name_hash) const
{
	// Locate first entry and search the collision chain for a matching hash
	const HashEntry* he = m_NamedObjects + name_hash % m_MaxNbObjects;
	while (he && he->hash != name_hash)
		he = he->next;

	// Return the object upon match
	if (he)
		return he->object;

	return 0;
}


void clutl::ObjectDatabase::AddHashEntry(Object* object)
{
	// Check the natural hash location
	HashEntry* he = m_NamedObjects + object->name.hash % m_MaxNbObjects;
	if (he->hash)
	{
		// Seek to the end of the collision chain
		while (he->next)
			he = he->next;

		// Run a linear probe on entries after the last in the chain to find a free slot
		HashEntry* free_entry = FindHashEntry(he - m_NamedObjects, 0);
		clcpp::internal::Assert(free_entry != 0);							// TODO: Grow based on load-factor?
		he->next = free_entry;
		he = free_entry;
	}

	// Add to the table
	he->hash = object->name.hash;
	he->object = object;
	he->next = 0;
}


void clutl::ObjectDatabase::RemoveHashEntry(const Object* object)
{
	// Get the natural hash location
	unsigned int name_hash = object->name.hash;
	HashEntry* he = m_NamedObjects + name_hash % m_MaxNbObjects;

	// Search the collision chain
	HashEntry* prev_entry = 0;
	while (he && he->hash != name_hash)
	{
		prev_entry = he;
		he = he->next;
	}

	clcpp::internal::Assert(he != 0);
	clcpp::internal::Assert(he->object == object);

	// Pull all collision hash entries one slot closer
	while (HashEntry* next_entry = he->next)
	{
		he->hash = next_entry->hash;
		he->object = next_entry->object;
		prev_entry = he;
		he = next_entry;
	}

	// Ensure the last entry in the collision chain has no next pointer
	if (prev_entry)
		prev_entry->next = 0;

	// Clear out this slot in the hash table
	he->hash = 0;
	he->object = 0;
	he->next = 0;
}


clutl::ObjectDatabase::HashEntry* clutl::ObjectDatabase::FindHashEntry(unsigned int hash_index, unsigned int hash)
{
	// Linear probe for an empty slot (search upper half/lower half to save a modulo)
	for (unsigned int i = hash_index; i < m_MaxNbObjects; i++)
	{
		HashEntry& he = m_NamedObjects[i];
		if (he.hash == hash)
			return &he;
	}
	for (unsigned int i = 0; i < hash_index; i++)
	{
		HashEntry& he = m_NamedObjects[i];
		if (he.hash == hash)
			return &he;
	}

	return 0;
}


clutl::ObjectIterator::ObjectIterator(const ObjectDatabase& object_db)
	: m_ObjectDB(object_db)
	, m_Position(0)
{
	// Search for the first non-empty slot
	ScanForEntry();
}


clutl::Object* clutl::ObjectIterator::GetObject() const
{
	clcpp::internal::Assert(IsValid());
	return m_ObjectDB.m_NamedObjects[m_Position].object;
}


void clutl::ObjectIterator::MoveNext()
{
	m_Position++;
	ScanForEntry();
}


bool clutl::ObjectIterator::IsValid() const
{
	return m_Position < m_ObjectDB.m_MaxNbObjects;
}


void clutl::ObjectIterator::ScanForEntry()
{
	// Search for the next non-empty slot
	while (m_Position < m_ObjectDB.m_MaxNbObjects &&
		m_ObjectDB.m_NamedObjects[m_Position].hash == 0)
		m_Position++;
}


/*void Add(clutl::ObjectDatabase& db, clutl::Object& object, unsigned int hash, unsigned int shifted)
{
	object.name.hash = db.GetMaxNbObjects() * shifted + hash;
	db.AddHashEntry(&object);
}


void TestObjectDatabaseHashing(const clcpp::Database* reflection_db)
{
	using namespace clcpp;
	using namespace clutl;

	ObjectDatabase db(reflection_db, 10);

	Object a, b, c, d, e, f, g, h;

	// Insert all objects such that they are linearly located in the hash array
	Add(db, a, 1, 0);
	Add(db, b, 1, 1);
	Add(db, c, 1, 2);
	Add(db, d, 4, 0);
	Add(db, e, 4, 1);
	Add(db, f, 4, 2);
	Add(db, g, 4, 3);
	Add(db, h, 1, 4);

	// Remove the central '1' object
	db.RemoveHashEntry(&b);

	// Remove the central '4' objects
	db.RemoveHashEntry(&e);
	db.RemoveHashEntry(&f);

	// Remove front and back entries
	db.RemoveHashEntry(&a);
	db.RemoveHashEntry(&g);

	// Add back some entries
	db.AddHashEntry(&b);
	db.AddHashEntry(&a);
	db.AddHashEntry(&f);

	// Remove '1' entries in reverse order
	db.RemoveHashEntry(&h);
	db.RemoveHashEntry(&c);
	db.RemoveHashEntry(&b);
	db.RemoveHashEntry(&a);

	// Remove '4' entries
	db.RemoveHashEntry(&f);
	db.RemoveHashEntry(&d);
}*/