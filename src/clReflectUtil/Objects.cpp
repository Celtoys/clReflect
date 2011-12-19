
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


void clutl::Object::Delete()
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


clutl::Object* clutl::ObjectDatabase::CreateAnonObject(unsigned int type_hash)
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


clutl::Object* clutl::ObjectDatabase::CreateNamedObject(unsigned int type_hash, const char* name_text)
{
	// Create the object
	Object* object = CreateAnonObject(type_hash);
	if (object == 0)
		return 0;

	// Construct the name
	int name_length = strlen(name_text);
	object->name.text = new char[name_length + 1];
	memcpy((void*)object->name.text, name_text, name_length + 1);
	object->name.hash = clcpp::internal::HashData(object->name.text, name_length);

	// Find the first empty slot in the hash table
	// TODO: Grow based on load-factor?
	HashEntry* he = const_cast<HashEntry*>(FindHashEntry(object->name.hash % m_MaxNbObjects, 0));
	clcpp::internal::Assert(he != 0);
	he->name = object->name;
	he->object = object;

	return object;
}


void clutl::ObjectDatabase::DestroyObject(Object* object)
{
	if (object->name.hash != 0)
	{
		// Locate the hash table entry and check that the object pointers match
		// TODO: Doesn't really work if insertion of an object required a probe and the objects before it
		// have been deleted.
		unsigned int name_hash = object->name.hash;
		HashEntry* he = const_cast<HashEntry*>(FindHashEntry(name_hash % m_MaxNbObjects, name_hash));
		clcpp::internal::Assert(he != 0);
		clcpp::internal::Assert(he->object == object);

		// Clear out this slot in the hash table
		he->name.hash = 0;
		if (he->name.text != 0)
			delete [] he->name.text;
		he->name.text = 0;
		he->object = 0;
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


clutl::Object* clutl::ObjectDatabase::FindNamedObject(unsigned int name_hash) const
{
	const HashEntry* he = FindHashEntry(name_hash % m_MaxNbObjects, name_hash);
	if (he)
		return he->object;
	return 0;
}


const clutl::ObjectDatabase::HashEntry* clutl::ObjectDatabase::FindHashEntry(unsigned int hash_index, unsigned int hash) const
{
	// Linear probe for an empty slot (search upper half/lower half to save a divide)
	for (unsigned int i = hash_index; i < m_MaxNbObjects; i++)
	{
		HashEntry& he = m_NamedObjects[i];
		if (he.name.hash == hash)
			return &he;
	}
	for (unsigned int i = 0; i < hash_index; i++)
	{
		HashEntry& he = m_NamedObjects[i];
		if (he.name.hash == hash)
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


clcpp::Name clutl::ObjectIterator::GetObjectName() const
{
	clcpp::internal::Assert(IsValid());
	return m_ObjectDB.m_NamedObjects[m_Position].name;
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
		m_ObjectDB.m_NamedObjects[m_Position].name.hash == 0)
		m_Position++;
}