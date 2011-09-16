
// TODO: Lots of stuff happening in here that needs logging

#include <clutl/Objects.h>
#include <clcpp/FunctionCall.h>


clutl::ObjectDatabase::ObjectDatabase(clcpp::Database& db)
	: m_ReflectionDB(db)
{
}


clutl::ObjectDatabase::~ObjectDatabase()
{
}


clutl::Object* clutl::ObjectDatabase::CreateObject(unsigned int type_hash)
{
	// Can the type be located?
	const clcpp::Type* type = m_ReflectionDB.GetType(type_hash);
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
	clutl::Object* object = (Object*)new char[type->size];
	CallFunction(class_type->constructor, object);
	object->type = type;

	return object;
}


void clutl::ObjectDatabase::DestroyObject(Object* object)
{
	// These represent fatal code errors
	clcpp::internal::Assert(object != 0);
	clcpp::internal::Assert(object->type != 0);

	// Call the destructor and release the memory
	const clcpp::Class* class_type = object->type->AsClass();
	clcpp::internal::Assert(class_type->destructor != 0);
	CallFunction(class_type->destructor, object);
	delete [] (char*)object;
}